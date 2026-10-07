#include <stdio.h>      /* printf, scanf, fflush, perror  */
#include <stdlib.h>     /* malloc, realloc, free, EXIT_*  */
#include <unistd.h>     /* close, write, alarm, _exit, STDOUT_FILENO */
#include <fcntl.h>      /* open, O_RDONLY                 */
#include <signal.h>     /* signal, SIGALRM, SIG_ERR       */
#include <sys/mman.h>   /* mmap, munmap                   */
#include <sys/stat.h>   /* fstat                          */
#include <string.h>     /* memcpy                         */
#include <errno.h>      /* errno                          */

#define TIMEOUT_SECONDS 5

/* Глобальные данные для доступа из обработчика сигнала */
static int g_fd = -1;
static char *g_mapped_data = NULL; // Указатель на начало файла в памяти
static size_t g_mapped_size = 0;   // Размер файла
static volatile sig_atomic_t g_timeout_triggered = 0;

/* ---------- таблица строк ---------- */

typedef struct {
    long offset;    /* Смещение от начала файла (в байтах) */
    int  length;    /* Длина строки без '\n'                */
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

/* Построение таблицы путем сканирования памяти (mmap) */
static int build_table_mmap(char *data, size_t size, LineTable *t)
{
    long line_start = 0;
    int  line_len = 0;
    
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n') {
            if (table_add(t, line_start, line_len) != 0)
                return -1;
            line_start = i + 1;
            line_len = 0;
        } else {
            line_len++;
        }
    }
    
    /* Обработка последней строки, если файл не заканчивается \n */
    if (line_len > 0 && table_add(t, line_start, line_len) != 0)
        return -1;

    return 0;
}

static void print_table(const LineTable *t)
{
    printf("--- Debug: Line Table ---\n");
    for (size_t i = 0; i < t->count; i++)
        printf("Line %zu: Offset = %ld, Length = %d\n",
               i + 1, t->items[i].offset, t->items[i].length);
    printf("-------------------------\n");
}

/* ---------- Асинхронная часть ---------- */

/* Безопасный вывод всего файла из памяти (async-signal-safe) */
static void dump_file_from_memory(void)
{
    if (g_mapped_data == NULL || g_mapped_size == 0)
        return;

    ssize_t written = 0;
    while (written < (ssize_t)g_mapped_size) {
        ssize_t w = write(STDOUT_FILENO, 
                          g_mapped_data + written, 
                          g_mapped_size - written);
        if (w <= 0) break;
        written += w;
    }
}

/* Обработчик SIGALRM */
static void alarm_handler(int sig)
{
    (void)sig;

    /* Отключаем будильник */
    alarm(0);

    /* Ставим флаг, что таймаут произошел */
    g_timeout_triggered = 1;

    /* Печатаем сообщение о таймауте */
    const char msg[] = "\n[TIMEOUT] Time is up! Printing full file content...\n";
    (void)!write(STDOUT_FILENO, msg, sizeof(msg) - 1);

    /* Печатаем файл напрямую из памяти */
    dump_file_from_memory();

    /* ВАЖНО: Мы НЕ делаем _exit() здесь! 
       Возвращаем управление в main, чтобы цикл мог увидеть флаг и завершиться. */
}

/* ---------- Чтение конкретной строки из памяти ---------- */

static int show_line_mmap(char *data, const LineInfo *li)
{
    /* Проверка границ */
    if ((size_t)(li->offset + li->length) > g_mapped_size) {
        return -1;
    }

    /* Выделяем буфер под строку + терминатор */
    char *buf = malloc((size_t)li->length + 1);
    if (!buf) return -1;

    /* Копируем данные из памяти в локальный буфер */
    memcpy(buf, data + li->offset, li->length);
    buf[li->length] = '\0';

    printf("%s\n", buf);
    
    free(buf);
    return 0;
}

int main(int argc, char *argv[])
{
    LineTable table = { NULL, 0, 0 };
    int num;
    int use_timer = 1; // Таймер активен только для первого ввода
    
    struct stat sb;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* 1. Открытие файла */
    g_fd = open(argv[1], O_RDONLY);
    if (g_fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    /* 2. Получение размера файла */
    if (fstat(g_fd, &sb) == -1) {
        perror("fstat");
        close(g_fd);
        return EXIT_FAILURE;
    }
    
    if (sb.st_size == 0) {
        fprintf(stderr, "File is empty.\n");
        close(g_fd);
        return EXIT_FAILURE;
    }

    g_mapped_size = sb.st_size;

    /* 3. Отображение файла в память (mmap) */
    g_mapped_data = mmap(NULL, g_mapped_size, PROT_READ, MAP_PRIVATE, g_fd, 0);
    if (g_mapped_data == MAP_FAILED) {
        perror("mmap");
        close(g_fd);
        return EXIT_FAILURE;
    }

    /* 4. Построение таблицы строк по данным в памяти */
    if (build_table_mmap(g_mapped_data, g_mapped_size, &table) != 0) {
        perror("build_table_mmap");
        munmap(g_mapped_data, g_mapped_size);
        close(g_fd);
        free(table.items);
        return EXIT_FAILURE;
    }

    print_table(&table);

    /* 5. Регистрация обработчика сигнала */
    if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
        perror("signal");
        munmap(g_mapped_data, g_mapped_size);
        close(g_fd);
        free(table.items);
        return EXIT_FAILURE;
    }

    /* Основной цикл взаимодействия с пользователем */
    for (;;) {
        g_timeout_triggered = 0;

        /* Установка таймера только если он еще активен */
        if (use_timer) {
            printf("Enter line number (0 to quit, %d sec timeout): ", TIMEOUT_SECONDS);
            fflush(stdout);
            alarm(TIMEOUT_SECONDS);
        } else {
            printf("Enter line number (0 to quit): ");
            fflush(stdout);
        }

        /* Блокирующий ввод */
        int rc = scanf("%d", &num);  

        /* Немедленно отменяем будильник, если он был установлен */
        if (use_timer) {
            alarm(0);
            
            /* Проверяем, сработал ли таймаут во время scanf */
            if (g_timeout_triggered) {
                /* Таймаут случился. Файл уже напечатан в хендлере.
                   Завершаем программу. */
                break; 
            }

            /* Если мы дошли сюда, значит пользователь успел.
               Выключаем таймер навсегда для следующих итераций. */
            use_timer = 0;
        }

        /* Обработка результатов scanf */
        if (rc == EOF) {
            break; /* Ctrl+D */
        } else if (rc != 1) {
            /* Введено не число. Чистим буфер. */
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            continue; /* Продолжаем цикл. Так как use_timer стал 0, следующий вход будет без таймера */
        }

        /* Здесь rc == 1, число прочитано успешно */
        
        if (num == 0) {
            break; /* Завершение работы по команде пользователя */
        }

        if (num < 1 || (size_t)num > table.count) {
            printf("No such line: there are %u lines in the file\n",
                   (unsigned)table.count);
            continue; /* Продолжаем цикл. Следующий вход будет без таймера */
        }

        /* Показываем строку, используя данные из памяти */
        if (show_line_mmap(g_mapped_data, &table.items[num - 1]) != 0) {
            perror("show_line_mmap");
            break;
        }
    }

    /* Очистка ресурсов */
    munmap(g_mapped_data, g_mapped_size);
    close(g_fd);
    free(table.items);
    
    return EXIT_SUCCESS;
}
