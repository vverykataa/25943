#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#define LINE_LIMIT 80
#define BUFFER_SIZE 4096

static int write_all(int fd, const unsigned char *data,
                     size_t length)
{
    while (length > 0)
    {
        ssize_t n = write(fd, data, length);

        if (n < 0)
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

        data += n;
        length -= (size_t)n;
    }

    return 0;
}

static int wait_ack(int fd)
{
    unsigned char ready;
    ssize_t count;

    do
    {
        count = read(fd, &ready, 1);
    } while (count == -1 && errno == EINTR);

    if (count == 1 && ready == 1)
        return 0;

    if (count >= 0)
        errno = EIO;

    return -1;
}

static int send_sync(int fd, int number)
{
    unsigned char role = (unsigned char)number;

    if (write_all(fd, &role, 1) == -1)
        goto error;

    if (wait_ack(fd) == -1)
        goto error;

    if (number == 1)
    {
        if (write_all(fd,
                      (const unsigned char *)"Hello ", 6) == -1)
            goto error;

        if (wait_ack(fd) == -1)
            goto error;

        if (write_all(fd,
                      (const unsigned char *)"World\n", 6) == -1)
            goto error;
    }
    else
    {
        if (write_all(fd,
                      (const unsigned char *)"good day\n", 9) == -1)
            goto error;
    }

    return EXIT_SUCCESS;

error:
    perror("sync client");
    return EXIT_FAILURE;
}

static void show_line(const unsigned char *line,
                      size_t length)
{
    fprintf(stderr, "\r\033[2K%.*s",
            (int)length, (const char *)line);
    fflush(stderr);
}

static int wait_foreground(void)
{
    pid_t foreground;

    while ((foreground = tcgetpgrp(STDIN_FILENO)) != -1 &&
           foreground != getpgrp())
    {
        if (raise(SIGSTOP) == -1)
            return -1;
    }

    return foreground == -1 ? -1 : 0;
}

static int send_terminal(int fd)
{
    struct termios original;
    struct termios settings;

    unsigned char line[LINE_LIMIT + 1];
    size_t length = 0;
    int escape = 0;
    int status = EXIT_SUCCESS;

    if (wait_foreground() == -1)
    {
        perror("wait foreground");
        return EXIT_FAILURE;
    }

    if (tcgetattr(STDIN_FILENO, &original) == -1)
    {
        perror("tcgetattr");
        return EXIT_FAILURE;
    }

    settings = original;
    settings.c_lflag &=
        (tcflag_t) ~(ICANON | ECHO | ISIG | IEXTEN);
    settings.c_iflag &=
        (tcflag_t) ~(IXON | IXOFF);
    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW,
                  &settings) == -1)
    {
        perror("tcsetattr");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Enter English text (max 80 characters)\n");
    fprintf(stderr, "Backspace: erase character\n");
    fprintf(stderr, "Ctrl+U: erase line\n");
    fprintf(stderr, "Ctrl+W: erase word\n");
    fprintf(stderr, "Ctrl+Z: suspend client\n");
    fprintf(stderr, "Ctrl+D: disconnect on empty line\n");

    for (;;)
    {
        unsigned char key;
        ssize_t n = read(STDIN_FILENO, &key, 1);

        if (n == 0)
            break;

        if (n == -1)
        {
            if (errno == EINTR)
                continue;
            perror("read stdin");
            status = EXIT_FAILURE;
            break;
        }

        if (escape == 1)
        {
            escape = (key == '[' || key == 'O') ? 2 : 0;
            continue;
        }

        if (escape == 2)
        {
            if (key >= 0x40 && key <= 0x7e)
                escape = 0;
            continue;
        }

        if (key == 27)
        {
            escape = 1;
            continue;
        }

        if (key == 26)
        {
            fputc('\n', stderr);

            if (tcsetattr(STDIN_FILENO, TCSANOW,
                          &original) == -1)
            {
                perror("restore terminal");
                status = EXIT_FAILURE;
                break;
            }

            if (raise(SIGSTOP) == -1 ||
                wait_foreground() == -1 ||
                tcsetattr(STDIN_FILENO, TCSANOW,
                          &settings) == -1)
            {
                perror("resume terminal");
                status = EXIT_FAILURE;
                break;
            }

            show_line(line, length);
        }
        else if (key == 3)
        {
            break;
        }
        else if (key == 4)
        {
            if (length == 0)
                break;
        }
        else if (key == 8 || key == 127)
        {
            if (length > 0)
                length--;
            show_line(line, length);
        }
        else if (key == 21)
        {
            length = 0;
            show_line(line, length);
        }
        else if (key == 23)
        {
            while (length > 0 && line[length - 1] == ' ')
                length--;

            while (length > 0 && line[length - 1] != ' ')
                length--;

            show_line(line, length);
        }
        else if (key == '\n' || key == '\r')
        {
            line[length++] = '\n';
            fputc('\n', stderr);

            if (write_all(fd, line, length) == -1)
            {
                perror("write socket");
                status = EXIT_FAILURE;
                break;
            }

            length = 0;
        }
        else if (key >= 32 && key <= 126)
        {
            if (length < LINE_LIMIT)
            {
                line[length++] = key;
                show_line(line, length);
            }
            else
            {
                fputc('\a', stderr);
                fflush(stderr);
            }
        }
    }

    fputc('\n', stderr);

    if (tcsetattr(STDIN_FILENO, TCSANOW,
                  &original) == -1)
    {
        perror("restore terminal");
        status = EXIT_FAILURE;
    }

    return status;
}

static int send_stdin(int fd)
{
    unsigned char buffer[BUFFER_SIZE];

    for (;;)
    {
        ssize_t n = read(STDIN_FILENO, buffer,
                         sizeof(buffer));

        if (n == 0)
            return EXIT_SUCCESS;

        if (n == -1)
        {
            if (errno == EINTR)
                continue;
            perror("read stdin");
            return EXIT_FAILURE;
        }

        if (write_all(fd, buffer, (size_t)n) == -1)
        {
            perror("write socket");
            return EXIT_FAILURE;
        }
    }
}

static int connect_server(const struct sockaddr_un *address,
                          int retry)
{
    for (int attempt = 0;; attempt++)
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd == -1)
            return -1;

        if (connect(fd, (const struct sockaddr *)address,
                    sizeof(*address)) == 0)
            return fd;

        int error = errno;
        close(fd);

        if (!retry || attempt >= 30 ||
            (error != ENOENT && error != ECONNREFUSED))
        {
            errno = error;
            return -1;
        }

        struct timespec delay = {0, 100000000L};
        nanosleep(&delay, NULL);
    }
}

int main(int argc, char *argv[])
{
    int sync_mode = 0;
    int number = 0;

    if (argc == 3 && strcmp(argv[1], "--sync") == 0 &&
        (strcmp(argv[2], "1") == 0 ||
         strcmp(argv[2], "2") == 0))
    {
        sync_mode = 1;
        number = atoi(argv[2]);
    }
    else if (argc != 1)
    {
        fprintf(stderr,
                "Usage: %s [--sync 1|2]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sockaddr_un address;
    char path[sizeof(address.sun_path)];

    int n = snprintf(path, sizeof(path),
                     "/tmp/task32_%ld.sock", (long)getuid());

    if (n < 0 || (size_t)n >= sizeof(path))
    {
        fprintf(stderr, "Socket path too long\n");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, (size_t)n + 1);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        return EXIT_FAILURE;
    }

    int connection = connect_server(&address, sync_mode);

    if (connection == -1)
    {
        perror("connect");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Client PID: %ld\n", (long)getpid());
    fprintf(stderr, "Connected to server\n");

    int status;

    if (sync_mode)
        status = send_sync(connection, number);
    else if (isatty(STDIN_FILENO))
        status = send_terminal(connection);
    else
        status = send_stdin(connection);

    close(connection);
    return status;
}