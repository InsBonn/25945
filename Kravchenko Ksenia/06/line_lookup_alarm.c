#include <stdio.h>      /* printf, scanf, fflush, perror  */
#include <stdlib.h>     /* malloc, realloc, free, EXIT_*  */
#include <unistd.h>     /* read, lseek, close, write,     */
                        /* alarm, _exit, STDOUT_FILENO    */
#include <fcntl.h>      /* open, O_RDONLY, SEEK_SET       */
#include <signal.h>     /* signal, SIGALRM, SIG_ERR       */

#define BLOCK_SIZE      4096
#define TIMEOUT_SECONDS 5

/* Глобальные данные: обработчик сигнала не получает аргументов,
   поэтому дескриптор файла приходится держать в глобальной области. */
static int g_fd = -1;

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

        if (p == NULL)
            return -1;
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

    while ((n = read(fd, buf, sizeof buf)) > 0) {
        /* позиция после чтения минус n = начало прочитанного блока */
        long base = (long)lseek(fd, 0L, SEEK_CUR) - (long)n;
        size_t i;

        if (base < 0)
            return -1;

        for (i = 0; i < (size_t)n; i++) {
            if (buf[i] == '\n') {
                if (table_add(t, line_start, line_len) != 0)
                    return -1;
                line_start = base + (long)i + 1;
                line_len = 0;
            } else {
                line_len++;
            }
        }
    }
    if (n < 0)
        return -1;

    if (line_len > 0 && table_add(t, line_start, line_len) != 0)
        return -1;

    return 0;
}

static void print_table(const LineTable *t)
{
    size_t i;

    printf("--- Debug: Line Table ---\n");
    for (i = 0; i < t->count; i++)
        printf("Line %u: Offset = %ld, Length = %d\n",
               (unsigned)(i + 1), t->items[i].offset, t->items[i].length);
    printf("-------------------------\n");
}

/* ---------- асинхронная часть: обработчик SIGALRM ---------- */

/* Печать всего файла. Здесь МОЖНО использовать только
   async-signal-safe вызовы: lseek/read/write.
   printf, malloc, free внутри обработчика недопустимы. */
static void dump_file(int fd)
{
    static char buf[BLOCK_SIZE];   /* не стек и не malloc */
    ssize_t n;

    if (lseek(fd, 0L, SEEK_SET) == (off_t)-1)   /* в начало файла */
        return;

    while ((n = read(fd, buf, sizeof buf)) > 0) {
        ssize_t off = 0;

        while (off < n) {                       /* write может записать меньше */
            ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));

            if (w <= 0)
                return;
            off += w;
        }
    }
}

/* Вызывается ядром через 5 секунд, если пользователь не ответил */
static void alarm_handler(int sig)
{
    static const char msg[] =
        "\n[TIMEOUT] Time is up! Printing full file content...\n";

    (void)sig;

    /* приглашение печаталось без '\n', поэтому начинаем с новой строки */
    (void)!write(STDOUT_FILENO, msg, sizeof msg - 1);

    if (g_fd != -1)
        dump_file(g_fd);

    /* _exit, а не exit: не трогаем буферы stdio из обработчика сигнала */
    _exit(EXIT_SUCCESS);
}

/* ---------- обычное чтение выбранной строки ---------- */

static int show_line(int fd, const LineInfo *li)
{
    char *buf = malloc((size_t)li->length + 1);
    int   got = 0;

    if (buf == NULL)
        return -1;

    if (lseek(fd, (off_t)li->offset, SEEK_SET) == (off_t)-1) {
        free(buf);
        return -1;
    }

    while (got < li->length) {
        ssize_t r = read(fd, buf + got, (size_t)(li->length - got));

        if (r < 0) {
            free(buf);
            return -1;
        }
        if (r == 0)
            break;
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

    /* Регистрируем обработчик: по умолчанию SIGALRM убивает процесс */
    if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
        perror("signal");
        close(g_fd);
        free(table.items);
        return EXIT_FAILURE;
    }

    for (;;) {
        int rc;

        printf("Enter line number (0 to quit, %d sec timeout): ",
               TIMEOUT_SECONDS);
        fflush(stdout);          /* иначе приглашение застрянет в буфере */

        alarm(TIMEOUT_SECONDS);  /* заводим будильник на 5 секунд */

        rc = scanf("%d", &num);  /* здесь процесс засыпает в ожидании ввода */

        if (rc == 1) {
            alarm(0);            /* успели — будильник отменяем */
        } else if (rc == EOF) {  /* конец ввода (Ctrl+D) */
            alarm(0);
            break;
        } else {                 /* введено не число — чистим строку */
            int c;

            alarm(0);
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            continue;
        }

        if (num == 0)
            break;

        if (num < 1 || (size_t)num > table.count) {
            printf("No such line: there are %u lines in the file\n",
                   (unsigned)table.count);
            continue;
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
