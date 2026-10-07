#include <stdio.h>      /* printf, scanf, fflush, perror  */
#include <stdlib.h>     /* malloc, realloc, free, EXIT_*  */
#include <unistd.h>     /* read, lseek, close, write, alarm, _exit */
#include <fcntl.h>      /* open, O_RDONLY, SEEK_SET       */
#include <signal.h>     /* signal, SIGALRM, SIG_ERR       */
#include <errno.h>      /* errno                          */

#define BLOCK_SIZE      4096
#define TIMEOUT_SECONDS 5

/* Глобальные данные */
static int g_fd = -1;
static volatile sig_atomic_t g_timeout_triggered = 0; // Флаг: сработал ли таймер сейчас?

/* ---------- таблица строк (как в задании 5) ---------- */

typedef struct {
    long offset;    /* позиция начала строки в файле */
    int  length;    /* длина строки без '\n'         */
} LineInfo;

typedef struct {
    LineInfo *items;
    size_t    count;
    size_t    capacity;
} LineTable;

static int table_add(LineTable *t, long offset, int length)
{
    if (t->count == t->capacity) {
        size_t new_cap = (t->capacity == 0) ? 16 : t->capacity * 2;
        LineInfo *p = realloc(t->items, new_cap * sizeof *p);
        if (p == NULL) return -1;
        t->items = p;
        t->capacity = new_cap;
    }
    t->items[t->count].offset = offset;
    t->items[t->count].length = length;
    t->count++;
    return 0;
}

/* Первый проход: отступы и длины всех строк */
static int build_table(int fd, LineTable *t)
{
    char    buf[BLOCK_SIZE];
    ssize_t n;
    long    line_start = 0;
    int     line_len = 0;
    long    file_pos = 0; 

    if (lseek(fd, 0L, SEEK_SET) == (off_t)-1) return -1;

    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                if (table_add(t, line_start, line_len) != 0)
                    return -1;
                line_start = file_pos + i + 1;
                line_len = 0;
            } else {
                line_len++;
            }
        }
        file_pos += n;
    }
    
    if (n < 0) return -1;
    if (line_len > 0 && table_add(t, line_start, line_len) != 0) return -1;

    return 0;
}

static void print_table(const LineTable *t)
{
    size_t i;
    printf("--- Debug: Line Table ---\n");
    for (i = 0; i < t->count; i++)
        printf("Line %zu: Offset = %ld, Length = %d\n",
               i + 1, t->items[i].offset, t->items[i].length);
    printf("-------------------------\n");
}

/* ---------- Асинхронная часть ---------- */

/* Безопасное чтение и вывод файла (async-signal-safe) */
static void dump_file_safe(int fd)
{
    static char buf[BLOCK_SIZE];   
    ssize_t n;

    if (lseek(fd, 0L, SEEK_SET) == (off_t)-1) return;

    while ((n = read(fd, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < n) {                       
            ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));
            if (w <= 0) return;
            off += w;
        }
    }
}

/* Обработчик SIGALRM */
static void alarm_handler(int sig)
{
    (void)sig;

    /* 1. Отключаем будильник, чтобы он не мешал дальше */
    alarm(0);

    /* 2. Ставим флаг, что таймаут произошел */
    g_timeout_triggered = 1;

    /* 3. Печатаем сообщение о таймауте */
    const char msg[] = "\n[TIMEOUT] Time is up! Printing full file content...\n";
    (void)!write(STDOUT_FILENO, msg, sizeof(msg) - 1);

    /* 4. Печатаем файл */
    if (g_fd != -1)
        dump_file_safe(g_fd);

    /* ВАЖНО: Мы НЕ делаем _exit() здесь! 
       Мы возвращаем управление в main, чтобы главный цикл мог увидеть флаг
       и принять решение о завершении программы. */
}

/* ---------- Обычное чтение выбранной строки ---------- */

static int show_line(int fd, const LineInfo *li)
{
    char *buf = malloc((size_t)li->length + 1);
    int   got = 0;

    if (buf == NULL) return -1;

    if (lseek(fd, (off_t)li->offset, SEEK_SET) == (off_t)-1) {
        free(buf);
        return -1;
    }

    while (got < li->length) {
        ssize_t r = read(fd, buf + got, (size_t)(li->length - got));
        if (r < 0) { free(buf); return -1; }
        if (r == 0) break;
        got += (int)r;
    }

    buf[got] = '\0';
    printf("%s\n", buf);
    free(buf);
    return 0;
}

int main(int argc, char *argv[])
{
    LineTable table = { NULL, 0, 0 };
    int num;
    int first_attempt_done = 0; // Флаг: была ли первая попытка с таймером

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    g_fd = open(argv[1], O_RDONLY);
    if (g_fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (build_table(g_fd, &table) != 0) {
        perror("build_table");
        close(g_fd);
        free(table.items);
        return EXIT_FAILURE;
    }

    print_table(&table);

    /* Регистрируем обработчик сигнала */
    if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
        perror("signal");
        close(g_fd);
        free(table.items);
        return EXIT_FAILURE;
    }

    for (;;) {
        /* Сбрасываем флаг таймаута перед каждым новым ожиданием */
        g_timeout_triggered = 0;

        /* Логика установки таймера */
        if (!first_attempt_done) {
            /* Это первый вход в цикл. Ставим таймер. */
            printf("Enter line number (0 to quit, %d sec timeout): ", TIMEOUT_SECONDS);
            fflush(stdout);
            alarm(TIMEOUT_SECONDS);
            
            int rc = scanf("%d", &num);
            
            /* Отменяем таймер немедленно после возврата из scanf, 
               независимо от того, успели мы или нет (на случай, если сигнал не пришел) */
            alarm(0); 
            
            /* Помечаем, что первая попытка пройдена */
            first_attempt_done = 1;

            /* Проверяем, сработал ли таймер во время ввода */
            if (g_timeout_triggered) {
                /* Таймаут случился. Файл уже напечатан в хендлере.
                   По условию задачи программа должна завершиться. */
                break; 
            }

            /* Если таймер не сработал, проверяем результат scanf */
            if (rc == EOF) {
                break; /* Ctrl+D */
            } else if (rc != 1) {
                /* Не число. Чистим буфер и продолжаем. 
                   Так как first_attempt_done=1, следующий цикл будет БЕЗ таймера */
                int c;
                while ((c = getchar()) != '\n' && c != EOF);
                continue;
            }
            
            /* Успешно прочитали число в первый раз. Идем на обработку ниже. */

        } else {
            /* Последующие попытки. Таймер НЕ ставится. Ждем ввода сколько угодно. */
            printf("Enter line number (0 to quit): ");
            fflush(stdout);
            
            int rc = scanf("%d", &num);
            
            if (rc == EOF) {
                break;
            } else if (rc != 1) {
                int c;
                while ((c = getchar()) != '\n' && c != EOF);
                continue;
            }
        }

        /* Общая обработка введенного числа num */
        if (num == 0) {
            break; /* Завершение работы */
        }

        if (num < 1 || (size_t)num > table.count) {
            printf("No such line: there are %u lines in the file\n",
                   (unsigned)table.count);
            continue; /* Продолжаем цикл. Если это было первое введение, 
                         следующий шаг пройдет через ветку 'else' (без таймера) */
        }

        if (show_line(g_fd, &table.items[num - 1]) != 0) {
            perror("show_line");
            break;
        }
    }

    close(g_fd);
    free(table.items);
    return EXIT_SUCCESS;
}
