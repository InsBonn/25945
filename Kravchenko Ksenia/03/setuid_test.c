#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

void print_uids() {
    printf("Real UID:      %d\n", getuid());
    printf("Effective UID: %d\n", geteuid());
}

void try_open(const char *label) {
    FILE *f = fopen("data.txt", "r");
    if (f) {
        printf("[%s] Файл открыт успешно!\n", label);
        fclose(f);
    } else {
        printf("[%s] Ошибка: ", label);
        perror("");
    }
}

int main() {
    printf("=== ДО сброса привилегий ===\n");
    print_uids();
    try_open("1");

    printf("\nСбрасываю привилегии: setuid(getuid())...\n");
    setuid(getuid());

    printf("\n=== ПОСЛЕ сброса привилегий ===\n");
    print_uids();
    try_open("2");

    return 0;
}