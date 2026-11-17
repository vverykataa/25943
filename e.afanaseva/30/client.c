#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>
#define INPUT_LIMIT 80
#define BUFFER_SIZE 4096
#define CONNECT_ATTEMPTS 30

static struct termios old_terminal;
static int terminal_changed = 0;

static volatile sig_atomic_t stopped = 0;
static volatile sig_atomic_t suspend_requested = 0;

static void restore_terminal(void)
{
    if (terminal_changed)
    {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH,
                        &old_terminal);
        terminal_changed = 0;
    }
}

static void signal_handler(int signum)
{
    if (signum == SIGTSTP)
        suspend_requested = 1;
    else
        stopped = signum;
}

static int wait_for_foreground(void)
{
    pid_t foreground;

    for (;;)
    {
        foreground = tcgetpgrp(STDIN_FILENO);

        if (foreground == -1)
        {
            perror("tcgetpgrp");
            return -1;
        }

        if (foreground == getpgrp())
            return 0;

        if (kill(0, SIGTTIN) == -1)
        {
            perror("SIGTTIN");
            return -1;
        }
    }
}

static int enable_terminal(void)
{
    struct termios settings = old_terminal;

    settings.c_lflag &= (tcflag_t) ~(
        ICANON | ECHO | ECHONL | ISIG | IEXTEN);

    settings.c_iflag &= (tcflag_t) ~(
        IXON | IXOFF | ISTRIP | INLCR | IGNCR);

    settings.c_iflag |= ICRNL;
    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH,
                  &settings) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    terminal_changed = 1;
    return 0;
}

static void redraw(const char *line, size_t length)
{
    fprintf(stderr, "\r\033[2K%.*s",
            (int)length, line);
    fflush(stderr);
}

static int suspend_input(const char *line, size_t length)
{
    restore_terminal();
    suspend_requested = 0;

    if (raise(SIGSTOP) != 0)
    {
        perror("raise");
        return -1;
    }

    if (wait_for_foreground() == -1)
        return -1;

    if (enable_terminal() == -1)
        return -1;

    redraw(line, length);
    return 0;
}

static int make_address(struct sockaddr_un *address,
                        const char *path)
{
    size_t length = strlen(path);

    if (length == 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (length >= sizeof(address->sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, path, length + 1);

    return 0;
}

static int write_all(int fd, const char *buffer,
                     size_t length)
{
    size_t written = 0;

    while (written < length)
    {
        ssize_t n = write(fd, buffer + written,
                          length - written);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            perror("write socket");
            return -1;
        }

        if (n == 0)
        {
            errno = EIO;
            perror("write socket");
            return -1;
        }

        written += (size_t)n;
    }

    return 0;
}

static int read_key(unsigned char *key, int timeout)
{
    struct pollfd input;
    int result;

    input.fd = STDIN_FILENO;
    input.events = POLLIN;

    while (!stopped && !suspend_requested)
    {
        input.revents = 0;

        result = poll(&input, 1,
                      timeout < 0 ? 100 : timeout);

        if (result < 0)
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

        ssize_t n = read(STDIN_FILENO, key, 1);

        if (n >= 0)
            return (int)n;

        if (errno != EINTR)
            return -1;
    }

    return -2;
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

static int terminal_input(int socket_fd)
{
    struct sigaction action;
    char line[INPUT_LIMIT + 1];
    size_t length = 0;
    unsigned char key;
    int status = EXIT_SUCCESS;

    if (wait_for_foreground() == -1)
        return EXIT_FAILURE;

    if (tcgetattr(STDIN_FILENO, &old_terminal) == -1)
    {
        perror("tcgetattr");
        return EXIT_FAILURE;
    }

    if (atexit(restore_terminal) != 0)
        return EXIT_FAILURE;

    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGQUIT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1 ||
        sigaction(SIGHUP, &action, NULL) == -1 ||
        sigaction(SIGTSTP, &action, NULL) == -1)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    if (enable_terminal() == -1)
        return EXIT_FAILURE;

    fprintf(stderr,
            "Enter English text (max 80 characters)\n"
            "Backspace: erase character\n"
            "Ctrl+U: erase line\n"
            "Ctrl+W: erase word\n"
            "Ctrl+Z: suspend client\n"
            "Ctrl+D: disconnect on empty line\n");

    while (!stopped)
    {
        if (suspend_requested)
        {
            if (suspend_input(line, length) == -1)
            {
                status = EXIT_FAILURE;
                break;
            }
            continue;
        }

        int result = read_key(&key, -1);

        if (result == -2)
            continue;

        if (result <= 0)
        {
            if (result < 0)
            {
                perror("read/poll");
                status = EXIT_FAILURE;
            }
            break;
        }

        if (key == 26)
        {
            suspend_requested = 1;
            continue;
        }
        else if (key == 3 || key == 28)
        {
            stopped = key == 3 ? SIGINT : SIGQUIT;
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
                 (old_terminal.c_cc[VERASE] !=
                      _POSIX_VDISABLE &&
                  key == old_terminal.c_cc[VERASE]))
        {
            if (length > 0)
                length--;

            redraw(line, length);
        }
        else if (key == 21 ||
                 (old_terminal.c_cc[VKILL] !=
                      _POSIX_VDISABLE &&
                  key == old_terminal.c_cc[VKILL]))
        {
            length = 0;
            redraw(line, length);
        }
        else if (key == 23)
        {
            while (length > 0 &&
                   line[length - 1] == ' ')
                length--;

            while (length > 0 &&
                   line[length - 1] != ' ')
                length--;

            redraw(line, length);
        }
        else if (key == '\n' || key == '\r')
        {
            fputc('\n', stderr);
            line[length++] = '\n';

            if (write_all(socket_fd, line, length) == -1)
            {
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

    return stopped ? 128 + (int)stopped : status;
}

static int transfer_input(int socket_fd)
{
    char buffer[BUFFER_SIZE];

    for (;;)
    {
        ssize_t n = read(STDIN_FILENO,
                         buffer, sizeof(buffer));

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            perror("read stdin");
            return EXIT_FAILURE;
        }

        if (n == 0)
            return EXIT_SUCCESS;

        if (write_all(socket_fd, buffer,
                      (size_t)n) == -1)
            return EXIT_FAILURE;
    }
}

static int connect_with_retry(int fd,
                              struct sockaddr_un *address)
{
    for (int attempt = 0;
         attempt < CONNECT_ATTEMPTS;
         attempt++)
    {
        if (connect(fd, (struct sockaddr *)address,
                    sizeof(*address)) == 0)
            return 0;

        if (errno != ENOENT && errno != ECONNREFUSED)
        {
            perror("connect");
            return -1;
        }

        if (attempt + 1 < CONNECT_ATTEMPTS)
            (void)poll(NULL, 0, 100);
    }

    perror("connect");
    return -1;
}

int main(int argc, char *argv[])
{
    int socket_fd;
    int status;
    char default_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *path;
    struct sockaddr_un address;
    struct sigaction action;

    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [socket_path]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int n = snprintf(default_path, sizeof(default_path),
                     "/tmp/task30_%ld.sock",
                     (long)getuid());

    if (n < 0 || (size_t)n >= sizeof(default_path))
    {
        fprintf(stderr, "Socket path is too long\n");
        return EXIT_FAILURE;
    }

    path = argc == 2 ? argv[1] : default_path;

    if (make_address(&address, path) == -1)
    {
        perror("socket path");
        return EXIT_FAILURE;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGPIPE, &action, NULL) == -1)
    {
        perror("sigaction SIGPIPE");
        return EXIT_FAILURE;
    }

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (connect_with_retry(socket_fd, &address) == -1)
    {
        fprintf(stderr, "Start server first: %s\n",
                path);
        close(socket_fd);
        return EXIT_FAILURE;
    }

    printf("Client PID: %ld\n", (long)getpid());
    printf("Connected to server\n");
    fflush(stdout);

    if (isatty(STDIN_FILENO))
        status = terminal_input(socket_fd);
    else
        status = transfer_input(socket_fd);

    if (close(socket_fd) == -1)
    {
        perror("close");
        return EXIT_FAILURE;
    }

    return status;
}