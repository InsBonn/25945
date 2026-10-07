#include <stdio.h>      /* printf, fprintf, fgets, stdin */
#include <stdlib.h>     /* malloc, free, EXIT_*          */
#include <string.h>     /* strlen, strcpy                */

#define MAX_LINE 1024   /* максимальная длина вводимой строки */

/* Узел списка: своя копия строки в куче + указатель на следующий узел */
struct Node {
    char *data;
    struct Node *next;
};

/* Добавление строки в конец списка.
   head — указатель на указатель, т.к. голова может измениться
   (список был пуст). Возврат: 0 — успех, -1 — не хватило памяти. */
static int append(struct Node **head, const char *str)
{
    struct Node *node;
    size_t len;

    node = malloc(sizeof *node);          /* память под узел */
    if (node == NULL)
        return -1;

    len = strlen(str);                    /* без завершающего '\0' */
    node->data = malloc(len + 1);         /* +1 байт под '\0'      */
    if (node->data == NULL) {
        free(node);
        return -1;
    }
    strcpy(node->data, str);              /* НЕЗАВИСИМАЯ копия строки */
    node->next = NULL;

    if (*head == NULL) {                  /* список был пуст */
        *head = node;
    } else {                              /* ищем последний узел */
        struct Node *cur = *head;
        while (cur->next != NULL)
            cur = cur->next;
        cur->next = node;
    }
    return 0;
}

/* Вывод всех строк списка по порядку */
static void print_list(const struct Node *head)
{
    const struct Node *cur;

    for (cur = head; cur != NULL; cur = cur->next)
        printf("%s\n", cur->data);
}

/* Полное освобождение: сначала строка, потом сам узел */
static void free_list(struct Node *head)
{
    while (head != NULL) {
        struct Node *next = head->next;
        free(head->data);
        free(head);
        head = next;
    }
}

int main(void)
{
    char buffer[MAX_LINE];
    struct Node *head = NULL;

    /* Читаем строки до точки в начале строки или до EOF */
    while (fgets(buffer, sizeof buffer, stdin) != NULL) {
        size_t len = strlen(buffer);

        /* fgets сохраняет '\n' — убираем его из конца строки */
        if (len > 0 && buffer[len - 1] == '\n')
            buffer[--len] = '\0';

        if (buffer[0] == '.')             /* точка в начале — конец ввода */
            break;

        if (len == 0)                     /* пустые строки пропускаем */
            continue;

        if (append(&head, buffer) != 0) {
            fprintf(stderr, "Ошибка: не удалось выделить память\n");
            free_list(head);              /* не течём памятью при сбое */
            return EXIT_FAILURE;
        }
    }

    print_list(head);                     /* вывод в порядке ввода */
    free_list(head);                      /* список освобождён */

    return EXIT_SUCCESS;
}
