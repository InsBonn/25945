#include <stdio.h>      /* printf, scanf, fprintf, perror */
#include <stdlib.h>     /* malloc, realloc, free, EXIT_*  */
#include <unistd.h>     /* read, lseek, close             */
#include <fcntl.h>      /* open, O_RDONLY, SEEK_SET,      */
                        /* SEEK_CUR                       */

#define BLOCK_SIZE 4096

/* Одна запись таблицы: где строка начинается и сколько в ней байт
   (без учёта разделителя '\n') */
typedef struct {
    long offset;
    int  length;
} LineInfo;

/* Таблица строк с динамическим расширением */
typedef struct {
    LineInfo *items;
    size_t    count;
    size_t    capacity;
} LineTable;

/* Добавить запись в таблицу; 0 — успех, -1 — нет памяти */
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

/* Первый проход: строим таблицу отступов и длин строк.
   Читаем блоками, а абсолютную позицию блока узнаём через
   lseek(fd, 0L, SEEK_CUR) — как советует задание.            */
static int build_table(int fd, LineTable *t)
{
    char    buf[BLOCK_SIZE];
    ssize_t n;
    long    line_start = 0;   /* первая строка начинается с нулевого отступа */
    int     line_len = 0;

    while ((n = read(fd, buf, sizeof buf)) > 0) {
        /* lseek(..., SEEK_CUR) возвращает позицию ПОСЛЕ чтения,
           поэтому начало блока = текущая позиция - n */
        long base = (long)lseek(fd, 0L, SEEK_CUR) - (long)n;
        size_t i;

        if (base < 0)
            return -1;

        for (i = 0; i < (size_t)n; i++) {
            if (buf[i] == '\n') {          /* конец строки найден */
                if (table_add(t, line_start, line_len) != 0)
                    return -1;
                line_start = base + (long)i + 1;
                line_len = 0;
            } else {
                line_len++;
            }
        }
    }
    if (n < 0)                             /* ошибка чтения */
        return -1;

    /* Последняя строка может быть без '\n' в конце файла */
    if (line_len > 0 && table_add(t, line_start, line_len) != 0)
        return -1;

    return 0;
}

/* Отладочная печать таблицы */
static void print_table(const LineTable *t)
{
    size_t i;

    printf("--- Debug: Line Table ---\n");
    for (i = 0; i < t->count; i++)
        printf("Line %u: Offset = %ld, Length = %d\n",
               (unsigned)(i + 1), t->items[i].offset, t->items[i].length);
    printf("-------------------------\n");
}

/* Прочитать и напечатать одну строку по её записи в таблице */
static int show_line(int fd, const LineInfo *li)
{
    char *buf = malloc((size_t)li->length + 1);   /* +1 под '\0' */
    int   got = 0;

    if (buf == NULL)
        return -1;

    if (lseek(fd, (off_t)li->offset, SEEK_SET) == (off_t)-1) {
        free(buf);
        return -1;
    }

    while (got < li->length) {                    /* читаем ровно length байт */
        ssize_t r = read(fd, buf + got, (size_t)(li->length - got));

        if (r < 0) {
            free(buf);
            return -1;
        }
        if (r == 0)                               /* файл короче ожидаемого */
            break;
        got += (int)r;
    }

    buf[got] = '\0';                              /* read не ставит '\0' сам */
    printf("%s\n", buf);
    free(buf);
    return 0;
}

int main(int argc, char *argv[])
{
    LineTable table = { NULL, 0, 0 };
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

    if (build_table(fd, &table) != 0) {
        perror("build_table");
        close(fd);
        free(table.items);
        return EXIT_FAILURE;
    }

    print_table(&table);            /* сверяем с таблицей, посчитанной вручную */

    for (;;) {
        printf("Enter line number (0 to quit): ");
        fflush(stdout);

        if (scanf("%d", &num) != 1)  /* не число или EOF — выходим */
            break;

        if (num == 0)
            break;

        if (num < 1 || (size_t)num > table.count) {
            printf("No such line: there are %u lines in the file\n",
                   (unsigned)table.count);
            continue;
        }

        /* нумерация для пользователя с 1, индекс массива — с 0 */
        if (show_line(fd, &table.items[num - 1]) != 0) {
            perror("show_line");
            break;
        }
    }

    close(fd);                      /* закрываем файл                    */
    free(table.items);              /* освобождаем память таблицы        */
    return EXIT_SUCCESS;
}
