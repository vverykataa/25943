#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/ulimit.h>
#include <sys/resource.h>
#include <errno.h>

extern char **environ;
extern long ulimit(int, ...);

struct option_item
{
    int opt;
    char *arg;
};

int main(int argc, char *argv[])
{
    int opt;
    struct option_item options[100];
    int option_count = 0;

    opterr = 0;

    while ((opt = getopt(argc, argv, "ispduU:cC:vV:")) != -1)
    {

        if (opt == '?')
        {
            if (optopt != 0)
            {
                fprintf(stderr,
                        "Unknown option or missing argument: -%c\n",
                        optopt);
            }
            else
            {
                fprintf(stderr, "Unknown option\n");
            }

            return 1;
        }

        if (option_count >= 100)
        {
            fprintf(stderr, "Too many options\n");
            return 1;
        }

        options[option_count].opt = opt;
        options[option_count].arg = optarg;
        option_count++;
    }

    for (int i = option_count - 1; i >= 0; i--)
    {

        switch (options[i].opt)
        {

        case 'i':
            printf("Real UID: %d\n", (int)getuid());
            printf("Effective UID: %d\n", (int)geteuid());
            printf("Real GID: %d\n", (int)getgid());
            printf("Effective GID: %d\n", (int)getegid());
            break;

        case 's':
            if (setpgid(0, 0) == -1)
            {
                perror("setpgid");
                return 1;
            }

            printf("Process became group leader\n");
            break;

        case 'p':
            printf("PID: %d\n", (int)getpid());
            printf("PPID: %d\n", (int)getppid());
            printf("PGID: %d\n", (int)getpgrp());
            break;

        case 'd':
        {
            char cwd[PATH_MAX];

            if (getcwd(cwd, sizeof(cwd)) == NULL)
            {
                perror("getcwd");
                return 1;
            }

            printf("Current directory: %s\n", cwd);
            break;
        }

        case 'u':
        {
            long limit = ulimit(UL_GETFSIZE);

            if (limit == -1)
            {
                perror("ulimit");
                return 1;
            }

            printf("Ulimit: %ld blocks\n", limit);
            break;
        }

        case 'U':
        {
            char *endptr;
            long new_limit;

            errno = 0;
            endptr = NULL;

            new_limit = strtol(options[i].arg, &endptr, 10);

            if (errno == ERANGE ||
                endptr == options[i].arg ||
                *endptr != '\0' ||
                new_limit < 0)
            {

                fprintf(stderr,
                        "Invalid ulimit value: %s\n",
                        options[i].arg);
                return 1;
            }

            if (ulimit(UL_SETFSIZE, new_limit) == -1)
            {
                perror("ulimit");
                return 1;
            }

            printf("Ulimit changed to %ld blocks\n", new_limit);
            break;
        }

        case 'c':
        {
            struct rlimit limit;

            if (getrlimit(RLIMIT_CORE, &limit) == -1)
            {
                perror("getrlimit");
                return 1;
            }

            if (limit.rlim_cur == RLIM_INFINITY)
            {
                printf("Core file size: unlimited\n");
            }
            else
            {
                printf("Core file size: %lu bytes\n",
                       (unsigned long)limit.rlim_cur);
            }

            break;
        }

        case 'C':
        {
            char *endptr;
            long new_size;
            struct rlimit limit;

            errno = 0;
            endptr = NULL;

            new_size = strtol(options[i].arg, &endptr, 10);

            if (errno == ERANGE ||
                endptr == options[i].arg ||
                *endptr != '\0' ||
                new_size < 0)
            {

                fprintf(stderr,
                        "Invalid core file size: %s\n",
                        options[i].arg);
                return 1;
            }

            if (getrlimit(RLIMIT_CORE, &limit) == -1)
            {
                perror("getrlimit");
                return 1;
            }

            limit.rlim_cur = (rlim_t)new_size;

            if (setrlimit(RLIMIT_CORE, &limit) == -1)
            {
                perror("setrlimit");
                return 1;
            }

            printf("Core file size changed to %ld bytes\n",
                   new_size);
            break;
        }

        case 'v':
        {
            char **env;

            for (env = environ; *env != NULL; env++)
            {
                printf("%s\n", *env);
            }

            break;
        }

        case 'V':
        {
            char *equals;

            equals = strchr(options[i].arg, '=');

            if (equals == NULL || equals == options[i].arg)
            {
                fprintf(stderr,
                        "Invalid environment variable: %s\n",
                        options[i].arg);
                return 1;
            }

            if (putenv(options[i].arg) != 0)
            {
                perror("putenv");
                return 1;
            }

            printf("Environment variable changed: %s\n",
                   options[i].arg);
            break;
        }

        default:
            break;
        }
    }

    return 0;
}
