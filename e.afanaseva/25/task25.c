#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define INPUT_LIMIT 80
#define BUFFER_SIZE 4096

static struct termios old_terminal;
static int terminal_changed = 0;
static volatile sig_atomic_t stopped = 0;

static void restore_terminal(void)
{
    if (terminal_changed)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_terminal);
        terminal_changed = 0;
    }
}

static void stop_handler(int signum)
{
    stopped = signum;
}

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length)
    {
        ssize_t n = write(fd, buffer + written, length - written);

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
        {
            errno = EIO;
            return -1;
        }

        written += (size_t)n;
    }

    return 0;
}

static int read_key(unsigned char *key, int timeout)
{
    struct pollfd pfd;
    ssize_t n;
    int result;

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    while (!stopped)
    {
        pfd.revents = 0;

        result = poll(&pfd, 1, timeout < 0 ? 100 : timeout);

        if (result == -1)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (result == 0)
        {
            if (timeout >= 0)
                return 0;

            continue;
        }

        n = read(STDIN_FILENO, key, 1);

        if (n >= 0)
            return (int)n;

        if (errno != EINTR)
            return -1;
    }

    return -1;
}

static void skip_escape(void)
{
    unsigned char key;
    int previous_escape = 0;

    if (read_key(&key, 100) != 1)
        return;

    if (key == '[' || key == 'O')
    {
        while (read_key(&key, 100) == 1)
        {
            if (key >= 0x40 && key <= 0x7e)
                return;
        }
    }
    else if (key == ']' || key == 'P' ||
             key == '^' || key == '_' || key == 'X')
    {
        while (read_key(&key, 100) == 1)
        {
            if (key == 7 ||
                (previous_escape && key == '\\'))
                return;

            previous_escape = (key == 27);
        }
    }
}

static void redraw(const char *line, size_t length)
{
    fprintf(stderr, "\r\033[2K%.*s", (int)length, line);
    fflush(stderr);
}

static int terminal_input(int destination)
{
    struct termios settings;
    struct sigaction action;
    char line[INPUT_LIMIT + 1];
    size_t length = 0;
    unsigned char key;
    int result;
    int status = EXIT_SUCCESS;

    if (tcgetattr(STDIN_FILENO, &old_terminal) == -1)
    {
        perror("tcgetattr");
        return EXIT_FAILURE;
    }

    if (atexit(restore_terminal) != 0)
        return EXIT_FAILURE;

    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGQUIT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1 ||
        sigaction(SIGHUP, &action, NULL) == -1)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    settings = old_terminal;

    settings.c_lflag &= ~(ICANON | ECHO | ECHONL |
                           ISIG | IEXTEN);

    settings.c_iflag &= ~(IXON | IXOFF | ISTRIP |
                           INLCR | IGNCR);

    settings.c_iflag |= ICRNL;

    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &settings) == -1)
    {
        perror("tcsetattr");
        return EXIT_FAILURE;
    }

    terminal_changed = 1;

    fprintf(stderr,
            "Enter English text (max 80 bytes per line)\n"
            "Backspace: erase character\n"
            "Ctrl+U: erase line\n"
            "Ctrl+W: erase word\n"
            "Ctrl+D: exit on empty line\n");

    while (!stopped)
    {
        result = read_key(&key, -1);

        if (result <= 0)
        {
            if (result < 0 && !stopped)
            {
                perror("read/poll");
                status = EXIT_FAILURE;
            }
            break;
        }

        if (key == 3 || key == 28)
        {
            stopped = (key == 3) ? SIGINT : SIGQUIT;
            break;
        }
        else if (key == 4)
        {
            if (length == 0)
                break;

            fputc('\a', stderr);
        }
        else if (key == 27)
        {
            skip_escape();
        }
        else if (key == 8 || key == 127 ||
                 key == old_terminal.c_cc[VERASE])
        {
            if (length > 0)
                length--;

            redraw(line, length);
        }
        else if (key == 21 ||
                 key == old_terminal.c_cc[VKILL])
        {
            length = 0;
            redraw(line, length);
        }
        else if (key == 23)
        {
            while (length > 0 && line[length - 1] == ' ')
                length--;

            while (length > 0 && line[length - 1] != ' ')
                length--;

            redraw(line, length);
        }
        else if (key == '\n' || key == '\r')
        {
            fputc('\n', stderr);

            line[length++] = '\n';

            if (write_all(destination, line, length) == -1)
            {
                perror("write pipe");
                status = EXIT_FAILURE;
                break;
            }

            length = 0;
        }
        else if (key >= 32 && key <= 126)
        {
            if (length < INPUT_LIMIT)
            {
                line[length++] = (char)key;
                redraw(line, length);
            }
            else
            {
                fputc('\a', stderr);
            }
        }
        else
        {
            fputc('\a', stderr);
        }

        fflush(stderr);
    }

    fputc('\n', stderr);
    restore_terminal();

    return stopped ? EXIT_FAILURE : status;
}

static int transfer(int source, int destination, int uppercase)
{
    char buffer[BUFFER_SIZE];

    while (1)
    {
        ssize_t n = read(source, buffer, sizeof(buffer));

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            perror("read");
            return EXIT_FAILURE;
        }

        if (n == 0)
            return EXIT_SUCCESS;

        if (uppercase)
        {
            for (ssize_t i = 0; i < n; i++)
            {
                buffer[i] =
                    (char)toupper((unsigned char)buffer[i]);
            }
        }

        if (write_all(destination, buffer, (size_t)n) == -1)
        {
            perror("write");
            return EXIT_FAILURE;
        }
    }
}

int main(void)
{
    int channel[2];
    pid_t pid;
    pid_t result;
    int status;
    int child_status;
    struct sigaction action;

    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGPIPE, &action, NULL) == -1)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    if (pipe(channel) == -1)
    {
        perror("pipe");
        return EXIT_FAILURE;
    }

    fflush(NULL);

    pid = fork();

    if (pid == -1)
    {
        perror("fork");
        close(channel[0]);
        close(channel[1]);
        return EXIT_FAILURE;
    }

    if (pid == 0)
    {
        close(channel[1]);

        status = transfer(channel[0], STDOUT_FILENO, 1);

        close(channel[0]);
        _exit(status);
    }

    close(channel[0]);

    fprintf(stderr,
            "Parent PID: %ld\n"
            "Child PID: %ld\n",
            (long)getpid(), (long)pid);

    if (isatty(STDIN_FILENO))
        status = terminal_input(channel[1]);
    else
        status = transfer(STDIN_FILENO, channel[1], 0);

    close(channel[1]);

    do
    {
        result = waitpid(pid, &child_status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1)
    {
        perror("waitpid");
        return EXIT_FAILURE;
    }

    if (!WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0)
    {
        return EXIT_FAILURE;
    }

    return status;
}