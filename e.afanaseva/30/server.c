#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define BUFFER_SIZE 4096

static volatile sig_atomic_t stopped = 0;

static void stop_handler(int signum)
{
    (void)signum;
    stopped = 1;
}

static int make_address(struct sockaddr_un *address,
                        const char *path)
{
    size_t length = strlen(path);

    if (length == 0 ||
        length >= sizeof(address->sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, path, length + 1);

    return 0;
}

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length)
    {
        ssize_t n = write(fd, buffer + written,
                          length - written);

        if (n == -1)
        {
            if (errno == EINTR)
            {
                if (stopped)
                    return -1;
                continue;
            }

            perror("write");
            return -1;
        }

        if (n == 0)
        {
            fprintf(stderr, "write returned zero\n");
            return -1;
        }

        written += (size_t)n;
    }

    return 0;
}

static int receive_text(int client_fd)
{
    char buffer[BUFFER_SIZE];

    while (!stopped)
    {
        ssize_t n = read(client_fd, buffer, sizeof(buffer));

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            perror("read socket");
            return EXIT_FAILURE;
        }

        if (n == 0)
            return EXIT_SUCCESS;

        for (ssize_t i = 0; i < n; i++)
        {
            buffer[i] =
                (char)toupper((unsigned char)buffer[i]);
        }

        if (write_all(STDOUT_FILENO, buffer,
                      (size_t)n) == -1)
        {
            return EXIT_FAILURE;
        }
    }

    return EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
    int server_fd = -1;
    int client_fd = -1;
    int status = EXIT_FAILURE;
    int bound = 0;

    char default_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *path;

    struct sockaddr_un address;
    struct sigaction action;

    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [socket_path]\n", argv[0]);
        return EXIT_FAILURE;
    }

    snprintf(default_path, sizeof(default_path),
             "/tmp/task30_%ld.sock", (long)getuid());

    path = argc == 2 ? argv[1] : default_path;

    if (make_address(&address, path) == -1)
    {
        perror("socket path");
        return EXIT_FAILURE;
    }

    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1 ||
        sigaction(SIGHUP, &action, NULL) == -1)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) == -1)
    {
        perror("bind");
        fprintf(stderr, "Socket path: %s\n", path);
        goto cleanup;
    }

    bound = 1;

    if (listen(server_fd, 1) == -1)
    {
        perror("listen");
        goto cleanup;
    }

    printf("Server PID: %ld\n", (long)getpid());
    printf("Socket: %s\n", path);
    printf("Waiting for client...\n");
    fflush(stdout);

    while (!stopped)
    {
        client_fd = accept(server_fd, NULL, NULL);

        if (client_fd == -1)
        {
            if (errno == EINTR)
                continue;

            perror("accept");
            goto cleanup;
        }

        break;
    }

    if (stopped)
        goto cleanup;

    printf("Client connected\n");
    fflush(stdout);

    status = receive_text(client_fd);

    if (!stopped && status == EXIT_SUCCESS)
    {
        printf("\nClient disconnected\n");
        fflush(stdout);
    }

cleanup:
    if (client_fd != -1)
        close(client_fd);

    if (server_fd != -1)
        close(server_fd);

    if (bound && unlink(path) == -1)
    {
        perror("unlink");
        status = EXIT_FAILURE;
    }

    return status;
}