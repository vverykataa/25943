#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int wait_for_child(pid_t child_pid, int *status)
{
    pid_t result;

    do
    {
        result = waitpid(child_pid, status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1)
    {
        perror("waitpid");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    pid_t child_pid;
    int status;
    int use_wait;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <1|2> <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argv[1][0] == '1' && argv[1][1] == '\0')
    {
        use_wait = 0;
    }
    else if (argv[1][0] == '2' && argv[1][1] == '\0')
    {
        use_wait = 1;
    }
    else
    {
        fprintf(stderr, "Mode must be 1 or 2\n");
        return EXIT_FAILURE;
    }

    fflush(NULL);

    child_pid = fork();

    if (child_pid == -1)
    {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (child_pid == 0)
    {
        printf("Child: PID = %ld, PPID = %ld\n",
               (long)getpid(), (long)getppid());
        fflush(stdout);

        execlp("cat", "cat", argv[2], (char *)NULL);

        perror("execlp cat");
        _exit(127);
    }

    printf("Parent: PID = %ld, child PID = %ld\n",
           (long)getpid(), (long)child_pid);

    if (use_wait)
    {
        printf("Parent: waiting for child...\n");
        fflush(stdout);

        if (wait_for_child(child_pid, &status) == -1)
        {
            return EXIT_FAILURE;
        }

        if (WIFEXITED(status))
        {
            printf("Child exit status: %d\n",
                   WEXITSTATUS(status));
        }
        else if (WIFSIGNALED(status))
        {
            printf("Child terminated by signal: %d\n",
                   WTERMSIG(status));
        }

        printf("Parent: final message after child completion\n");

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            return EXIT_FAILURE;
        }
    }
    else
    {
        printf("Parent: final message without waiting\n");
    }

    fflush(stdout);

    return EXIT_SUCCESS;
}