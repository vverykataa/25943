#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    pid_t pid;
    int status;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s command [arguments...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    pid = fork();

    if (pid == -1)
    {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0)
    {
        execvp(argv[1], &argv[1]);

        perror("execvp");
        _exit(127);
    }

    while (waitpid(pid, &status, 0) == -1)
    {
        if (errno == EINTR)
        {
            continue;
        }

        perror("waitpid");
        return EXIT_FAILURE;
    }

    if (WIFEXITED(status))
    {
        printf("Command exit status: %d\n", WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        printf("Command terminated by signal: %d\n", WTERMSIG(status));
    }

    return EXIT_SUCCESS;
}