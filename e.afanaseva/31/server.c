#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define MAX_CLIENTS 64
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

    if (length == 0 || length >= sizeof(address->sun_path))
    {
        errno = length == 0 ? EINVAL : ENAMETOOLONG;
        return -1;
    }

    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, path, length + 1);
    return 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags == -1)
        return -1;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length)
    {
        ssize_t n = write(fd, buffer + written, length - written);

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

        written += (size_t)n;
    }

    return 0;
}

static int free_slot(struct pollfd fds[])
{
    for (int i = 1; i <= MAX_CLIENTS; i++)
    {
        if (fds[i].fd == -1)
            return i;
    }

    return -1;
}

static void close_client(struct pollfd fds[], int ids[],
                         int index)
{
    fprintf(stderr, "Client %d disconnected\n", ids[index]);

    close(fds[index].fd);
    fds[index].fd = -1;
    fds[index].events = 0;
    fds[index].revents = 0;
    ids[index] = 0;
}

/*
 * Synchronization:
 * stage 0: wait for two clients
 * stage 1: client 1 sends "Hello "
 * stage 2: client 2 sends "good day\n"
 * stage 3: client 1 sends "World\n"
 * stage 4: demonstration completed
 */

static int process_sync(struct pollfd fds[],
                        int roles[], size_t received[],
                        int index, int *stage)
{
    static const char *const parts[] = {
        NULL, "Hello ", "good day\n", "World\n"};
    static const size_t lengths[] = {0, 6, 9, 6};

    unsigned char buffer[BUFFER_SIZE];
    int role = roles[index];

    ssize_t n = read(fds[index].fd, buffer, sizeof(buffer));

    if (n < 0)
    {
        if (errno == EINTR || would_block())
            return 0;

        perror("read sync");
        return -1;
    }

    if (n == 0)
        return 1;

    if (role == 0)
    {
        if (n != 1 || (buffer[0] != 1 && buffer[0] != 2))
        {
            fprintf(stderr, "Invalid sync client ID\n");
            return -1;
        }

        for (int i = 1; i <= MAX_CLIENTS; i++)
        {
            if (i != index && roles[i] == buffer[0])
            {
                fprintf(stderr, "Duplicate sync client ID\n");
                return -1;
            }
        }

        roles[index] = buffer[0];

        fprintf(stderr, "Sync client %d registered\n",
                roles[index]);

        int first = -1;
        int second = -1;

        for (int i = 1; i <= MAX_CLIENTS; i++)
        {
            if (fds[i].fd == -1)
                continue;

            if (roles[i] == 1)
                first = i;
            else if (roles[i] == 2)
                second = i;
        }

        if (*stage == 0 && first != -1 && second != -1)
        {
            *stage = 1;
            fds[first].events |= POLLOUT;
        }

        return 0;
    }

    int expected_role = *stage == 2 ? 2 : 1;

    if (*stage < 1 || *stage > 3 || role != expected_role)
    {
        fprintf(stderr, "Unexpected sync data\n");
        return -1;
    }

    if ((size_t)n > lengths[*stage] - received[index] ||
        memcmp(buffer, parts[*stage] + received[index],
               (size_t)n) != 0)
    {
        fprintf(stderr, "Invalid sync message\n");
        return -1;
    }

    for (ssize_t i = 0; i < n; i++)
        buffer[i] = (unsigned char)toupper(buffer[i]);

    if (write_all(STDOUT_FILENO, (const char *)buffer,
                  (size_t)n) == -1)
    {
        perror("write stdout");
        return -1;
    }

    received[index] += (size_t)n;

    if (received[index] == lengths[*stage])
    {
        if (*stage == 1)
        {
            *stage = 2;
            received[index] = 0;

            for (int i = 1; i <= MAX_CLIENTS; i++)
                if (roles[i] == 2)
                    fds[i].events |= POLLOUT;
        }
        else if (*stage == 2)
        {
            *stage = 3;
            received[index] = 0;

            for (int i = 1; i <= MAX_CLIENTS; i++)
                if (roles[i] == 1)
                    fds[i].events |= POLLOUT;
        }
        else
        {
            *stage = 4;
            fprintf(stderr, "\nSync demonstration completed\n");
        }
    }

    return 0;
}

static int serve(int listener, int sync_mode)
{
    struct pollfd fds[MAX_CLIENTS + 1];
    int ids[MAX_CLIENTS + 1];
    int roles[MAX_CLIENTS + 1];
    size_t received[MAX_CLIENTS + 1];

    int next_id = 1;
    int accepted = 0;
    int closed = 0;
    int stage = 0;
    int status = EXIT_SUCCESS;

    for (int i = 0; i <= MAX_CLIENTS; i++)
    {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
        ids[i] = 0;
        roles[i] = 0;
        received[i] = 0;
    }

    fds[0].fd = listener;
    fds[0].events = POLLIN;

    while (!stopped)
    {
        int available = free_slot(fds);

        fds[0].events =
            available != -1 && (!sync_mode || accepted < 2)
                ? POLLIN
                : 0;

        int ready = poll(fds, MAX_CLIENTS + 1, -1);

        if (ready == -1)
        {
            if (errno == EINTR)
                continue;

            perror("poll");
            status = EXIT_FAILURE;
            break;
        }

        if (fds[0].revents & (POLLERR | POLLNVAL | POLLHUP))
        {
            fprintf(stderr, "Listening socket error\n");
            status = EXIT_FAILURE;
            break;
        }

        if (fds[0].revents & POLLIN)
        {
            for (;;)
            {
                int slot = free_slot(fds);

                if (slot == -1 || (sync_mode && accepted >= 2))
                    break;

                int fd = accept(listener, NULL, NULL);

                if (fd == -1)
                {
                    if (errno == EINTR)
                        continue;

                    if (would_block())
                        break;

                    perror("accept");
                    status = EXIT_FAILURE;
                    goto cleanup;
                }

                if (set_nonblocking(fd) == -1)
                {
                    perror("fcntl client");
                    close(fd);
                    continue;
                }

                fds[slot].fd = fd;
                fds[slot].events = POLLIN;
                fds[slot].revents = 0;
                ids[slot] = next_id++;
                roles[slot] = 0;
                received[slot] = 0;

                accepted++;

                fprintf(stderr, "Client %d connected (fd=%d)\n",
                        ids[slot], fd);
            }
        }

        for (int i = 1; i <= MAX_CLIENTS; i++)
        {
            if (fds[i].fd == -1)
                continue;

            short events = fds[i].revents;

            if (events & POLLNVAL)
            {
                status = EXIT_FAILURE;
                goto cleanup;
            }

            if (events & POLLOUT)
            {
                unsigned char ack = 1;
                ssize_t n = write(fds[i].fd, &ack, 1);

                if (n == 1)
                {
                    fds[i].events &= (short)~POLLOUT;
                }
                else if (n < 0 &&
                         (errno == EINTR || would_block()))
                {
                    /* Retry when poll reports POLLOUT again. */
                }
                else
                {
                    perror("sync acknowledgement");
                    status = EXIT_FAILURE;
                    goto cleanup;
                }
            }

            if (events & (POLLIN | POLLHUP | POLLERR))
            {
                if (sync_mode)
                {
                    int result = process_sync(
                        fds, roles, received, i, &stage);

                    if (result == -1)
                    {
                        status = EXIT_FAILURE;
                        goto cleanup;
                    }

                    if (result == 1)
                    {
                        if ((roles[i] == 1 && stage < 4) ||
                            (roles[i] == 2 && stage < 3))
                        {
                            fprintf(stderr,
                                    "Sync client disconnected early\n");
                            status = EXIT_FAILURE;
                            goto cleanup;
                        }

                        close_client(fds, ids, i);
                        closed++;
                    }
                }
                else
                {
                    unsigned char buffer[BUFFER_SIZE];

                    ssize_t n = read(fds[i].fd,
                                     buffer, sizeof(buffer));

                    if (n == 0)
                    {
                        close_client(fds, ids, i);
                    }
                    else if (n < 0)
                    {
                        if (errno != EINTR && !would_block())
                        {
                            perror("read socket");
                            close_client(fds, ids, i);
                        }
                    }
                    else
                    {
                        for (ssize_t j = 0; j < n; j++)
                            buffer[j] =
                                (unsigned char)toupper(buffer[j]);

                        if (write_all(STDOUT_FILENO,
                                      (const char *)buffer,
                                      (size_t)n) == -1)
                        {
                            perror("write stdout");
                            status = EXIT_FAILURE;
                            goto cleanup;
                        }
                    }
                }
            }
        }

        if (sync_mode && stage == 4 && closed == 2)
        {
            fprintf(stderr, "Both sync clients disconnected\n");
            break;
        }
    }

cleanup:
    for (int i = 1; i <= MAX_CLIENTS; i++)
        if (fds[i].fd != -1)
            close(fds[i].fd);

    return status;
}

int main(int argc, char *argv[])
{
    struct sockaddr_un address;
    struct sigaction action;

    char default_path[sizeof(address.sun_path)];
    const char *path;

    int sync_mode = argc > 1 && strcmp(argv[1], "--sync") == 0;
    int path_arg = sync_mode ? 2 : 1;
    int listener = -1;
    int bound = 0;
    int status = EXIT_FAILURE;

    if (argc > path_arg + 1)
    {
        fprintf(stderr, "Usage: %s [--sync] [socket_path]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int n = snprintf(default_path, sizeof(default_path),
                     "/tmp/task31_%ld.sock", (long)getuid());

    if (n < 0 || (size_t)n >= sizeof(default_path))
    {
        fprintf(stderr, "Socket path is too long\n");
        return EXIT_FAILURE;
    }

    path = argc > path_arg ? argv[path_arg] : default_path;

    if (make_address(&address, path) == -1)
    {
        perror("socket path");
        return EXIT_FAILURE;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1 ||
        sigaction(SIGHUP, &action, NULL) == -1)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    listener = socket(AF_UNIX, SOCK_STREAM, 0);

    if (listener == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (set_nonblocking(listener) == -1)
    {
        perror("fcntl listener");
        goto cleanup;
    }

    if (bind(listener, (struct sockaddr *)&address,
             sizeof(address)) == -1)
    {
        perror("bind");
        fprintf(stderr, "Socket path: %s\n", path);
        goto cleanup;
    }

    bound = 1;

    if (listen(listener, MAX_CLIENTS) == -1)
    {
        perror("listen");
        goto cleanup;
    }

    printf("Server PID: %ld\n", (long)getpid());
    printf("Socket: %s\n", path);
    printf("Mode: %s\n", sync_mode ? "synchronized" : "normal");
    printf("Waiting for clients...\n");
    fflush(stdout);

    status = serve(listener, sync_mode);

cleanup:
    if (listener != -1)
        close(listener);

    if (bound && unlink(path) == -1)
    {
        perror("unlink");
        status = EXIT_FAILURE;
    }

    return status;
}