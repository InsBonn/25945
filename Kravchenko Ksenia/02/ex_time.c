#include <stdio.h>
#include <time.h>
#include <stdlib.h>

int main(void) {
    // 1. Установка часового пояса Калифорнии и применение настроек
    // "1" в конце означает перезаписать переменную, если она уже существует
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();

    // 2. Получение текущего времени (количество секунд с 01.01.1970 UTC)
    time_t now;
    time(&now);

    // 3. Конвертация абсолютного времени в локальное с учетом установленного TZ
    struct tm *sp = localtime(&now);
    
    // Небольшая проверка на случай сбоя (хотя для текущего времени это редкость)
    if (sp == NULL) {
        perror("Ошибка localtime");
        return 1;
    }

    // 4. Форматированный вывод
    // sp->tm_mday: день месяца (1-31)
    // sp->tm_mon + 1: месяц (0-11, поэтому прибавляем 1)
    // sp->tm_year + 1900: год (считается от 1900)
    // sp->tm_hour: часы (0-23)
    // sp->tm_min: минуты (0-59)
    // tzname[sp->tm_isdst]: название пояса (PST или PDT в зависимости от флага летнего времени)
    printf("%02d/%02d/%04d %02d:%02d %s\n",
           sp->tm_mday,
           sp->tm_mon + 1,
           sp->tm_year + 1900,
           sp->tm_hour,
           sp->tm_min,
           tzname[sp->tm_isdst]);

    return 0;
}