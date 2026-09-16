#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

extern char **environ;

/* Структура для хранения одной опции */
typedef struct {
    int opt;          /* буква опции */
    char *arg;        /* аргумент опции (если есть) */
} Option;

int main(int argc, char *argv[]) {
    char *optstring = "ispuU:cC:dvV:";
    int c;
    
    Option options[256];   /* массив для хранения опций */
    int opt_count = 0;
    
    /* ========== 1. Собираем ВСЕ опции слева направо ========== */
    while ((c = getopt(argc, argv, optstring)) != -1) {
        if (opt_count >= 256) {
            fprintf(stderr, "Too many options\n");
            return 1;
        }
        
        options[opt_count].opt = c;
        
        if (optarg != NULL) {
            options[opt_count].arg = strdup(optarg);  /* копируем аргумент */
        } else {
            options[opt_count].arg = NULL;
        }
        
        opt_count++;
        
        /* Обработка ошибки getopt */
        if (c == '?') {
            /* Неправильная опция уже обработана getopt'ом,
               но мы всё равно сохраним её, чтобы потом напечатать */
        }
    }
    
    /* ========== 2. Выполняем опции СПРАВА НАЛЕВО ========== */
    for (int i = opt_count - 1; i >= 0; i--) {
        c = options[i].opt;
        char *arg = options[i].arg;
        
        switch (c) {
            /* ----- -i : реальный и эффективный UID/GID ----- */
            case 'i': {
                printf("Real UID = %d, Effective UID = %d\n", getuid(), geteuid());
                printf("Real GID = %d, Effective GID = %d\n", getgid(), getegid());
                break;
            }
            
            /* ----- -s : стать лидером группы процессов ----- */
            case 's': {
                if (setpgid(0, 0) == -1) {
                    perror("setpgid");
                } else {
                    printf("Process became group leader (PGID = %d)\n", getpgrp());
                }
                break;
            }
            
            /* ----- -p : PID, PPID, PGID ----- */
            case 'p': {
                printf("PID = %d, PPID = %d, PGID = %d\n",
                       getpid(), getppid(), getpgrp());
                break;
            }
            
            /* ----- -u : текущий ulimit (RLIMIT_NOFILE) ----- */
            case 'u': {
                struct rlimit rl;
                if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
                    perror("getrlimit");
                } else {
                    printf("ulimit (RLIMIT_NOFILE) = %llu\n",
                           (unsigned long long)rl.rlim_cur);
                }
                break;
            }
            
            /* ----- -Unew_ulimit : изменить ulimit ----- */
            case 'U': {
                char *endptr;
                long new_limit = strtol(arg, &endptr, 10);
                
                if (*endptr != '\0' || new_limit < 0) {
                    fprintf(stderr, "Invalid value for -U: %s\n", arg);
                    break;
                }
                
                struct rlimit rl;
                if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
                    perror("getrlimit");
                    break;
                }
                
                rl.rlim_cur = (rlim_t)new_limit;
                /* Не трогаем rlim_max, если new_limit больше — setrlimit сам откажет */
                
                if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
                    perror("setrlimit");
                } else {
                    printf("ulimit changed to %ld\n", new_limit);
                }
                break;
            }
            
            /* ----- -c : размер core-файла ----- */
            case 'c': {
                struct rlimit rl;
                if (getrlimit(RLIMIT_CORE, &rl) == -1) {
                    perror("getrlimit");
                } else {
                    printf("core file size = %llu bytes\n",
                           (unsigned long long)rl.rlim_cur);
                }
                break;
            }
            
            /* ----- -Csize : изменить размер core-файла ----- */
            case 'C': {
                char *endptr;
                long new_size = strtol(arg, &endptr, 10);
                
                if (*endptr != '\0' || new_size < 0) {
                    fprintf(stderr, "Invalid value for -C: %s\n", arg);
                    break;
                }
                
                struct rlimit rl;
                if (getrlimit(RLIMIT_CORE, &rl) == -1) {
                    perror("getrlimit");
                    break;
                }
                
                rl.rlim_cur = (rlim_t)new_size;
                
                if (setrlimit(RLIMIT_CORE, &rl) == -1) {
                    perror("setrlimit");
                } else {
                    printf("core file size changed to %ld bytes\n", new_size);
                }
                break;
            }
            
            /* ----- -d : текущая директория ----- */
            case 'd': {
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    perror("getcwd");
                } else {
                    printf("Current directory: %s\n", cwd);
                }
                break;
            }
            
            /* ----- -v : все переменные окружения ----- */
            case 'v': {
                for (char **env = environ; *env != NULL; env++) {
                    printf("%s\n", *env);
                }
                break;
            }
            
            /* ----- -Vname=value : добавить/изменить переменную ----- */
            case 'V': {
                if (putenv(arg) != 0) {
                    perror("putenv");
                } else {
                    printf("Environment variable set: %s\n", arg);
                }
                break;
            }
            
            /* ----- Неправильная опция ----- */
            case '?': {
                /* getopt уже вывел сообщение в stderr,
                   но на всякий случай можно добавить своё */
                break;
            }
            
            default:
                break;
        }
        
        /* Освобождаем память, если выделяли */
        if (arg != NULL) {
            free(arg);
        }
    }
    
    return 0;
}