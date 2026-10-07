#include <stdio.h>      /* printf, scanf, fflush, fwrite  */
#include <stdlib.h>     /* malloc, realloc, free, EXIT_*  */
#include <unistd.h>     /* close, alarm, _exit, write     */
#include <fcntl.h>      /* open, O_RDONLY                 */
#include <signal.h>     /* signal, SIGALRM, SIG_ERR       */
#include <sys/mman.h>   /* mmap, munmap, MAP_FAILED       */
#include <sys/stat.h>   /* fstat, struct stat             */

#define TIMEOUT_SECONDS 5

/* Отображение доступно и обработчику сигнала — держим в глобальных
   переменных: файл ведёт себя как обычный массив байтов. */
static const char *g_map  = NULL;
static size_t      g_size = 0;

/* ---------- таблица строк ---------- */

typedef struct {
    size_t offset;   /* смещение начала строки от начала файла */
    size_t length;   /* длина строки без '\n'                  */
} LineInfo;

typedef struct {
    LineInfo *items;
    size_t    count;
    size_t    capacity;
} LineTable;

static int table_add(LineTable *t, size_t offset, size_t length)
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

/* Таблица строится простым проходом по памяти — ни lseek, ни read */
static int build_table(LineTable *t)
{
    size_t line_start = 0;   /* первая строка начинается с нулевого отступа */
    size_t i;

    for (i = 0; i < g_size; i++) {
        if (g_map[i] == '\n') {
            if (table_add(t, line_start, i - line_start) != 0)
                return -1;
            line_start = i + 1;
        }
    }

    /* последняя строка может быть без '\n' в конце файла */
    if (line_start < g_size &&
        table_add(t, line_start, g_size - line_start) != 0)
        return -1;

    return 0;
}

static void print_table(const LineTable *t)
{
    size_t i;

    printf("--- Debug: Line Table ---\n");
    for (i = 0; i < t->count; i++)
        printf("Line %u: Offset = %lu, Length = %lu\n",
               (unsigned)(i + 1),
               (unsigned long)t->items[i].offset,
               (unsigned long)t->items[i].length);
    printf("-------------------------\n");
}

/* ---------- обработчик SIGALRM ---------- */

static void alarm_handler(int sig)
{
    static const char msg[] =
        "\n[TIMEOUT] Time is up! Printing full file content...\n";
    size_t off = 0;

    (void)sig;

    (void)!write(STDOUT_FILENO, msg, sizeof msg - 1);

    /* Всё содержимое файла уже в памяти — отдаём одним куском
       через write (async-signal-safe), с учётом частичной записи. */
    if (g_map != NULL) {
        while (off < g_size) {
            ssize_t w = write(STDOUT_FILENO, g_map + off, g_size - off);

            if (w <= 0)
                break;
            off += (size_t)w;
        }
    }

    _exit(EXIT_SUCCESS);    /* без очистки буферов stdio */
}

/* ---------- вывод выбранной строки ---------- */

/* fwrite печатает ровно length байт — '\0' не нужен,
   и строка не «уезжает» за границу своей длины.       */
static void show_line(const LineInfo *li)
{
    fwrite(g_map + li->offset, 1, li->length, stdout);
    fputc('\n', stdout);
}

int main(int argc, char *argv[])
{
    LineTable table = { NULL, 0, 0 };
    struct stat st;
    int fd, num;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (fstat(fd, &st) == -1) {          /* размер нужен для mmap */
        perror("fstat");
        close(fd);
        return EXIT_FAILURE;
    }

    if (st.st_size == 0) {               /* mmap нулевой длины не работает */
        printf("File is empty.\n");
        close(fd);
        return EXIT_SUCCESS;
    }

    g_size = (size_t)st.st_size;
    g_map = mmap(NULL, g_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (g_map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);      /* отображение живёт независимо от дескриптора */

    if (build_table(&table) != 0) {
        perror("build_table");
        munmap((void *)g_map, g_size);
        free(table.items);
        return EXIT_FAILURE;
    }

    print_table(&table);

    if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
        perror("signal");
        munmap((void *)g_map, g_size);
        free(table.items);
        return EXIT_FAILURE;
    }

    for (;;) {
        int rc;

        printf("Enter line number (0 to quit, %d sec timeout): ",
               TIMEOUT_SECONDS);
        fflush(stdout);

        alarm(TIMEOUT_SECONDS);
        rc = scanf("%d", &num);

        if (rc == 1) {
            alarm(0);
        } else if (rc == EOF) {
            alarm(0);
            break;
        } else {
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

        show_line(&table.items[num - 1]);   /* нумерация с 1, индекс с 0 */
    }

    alarm(0);                        /* сначала снимаем будильник,      */
    munmap((void *)g_map, g_size);   /* потом отвязываем отображение    */
    g_map = NULL;                    /* обработчику больше нечего читать */
    free(table.items);

    return EXIT_SUCCESS;
}
