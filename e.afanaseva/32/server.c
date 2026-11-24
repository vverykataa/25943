#define _POSIX_C_SOURCE 200809L
#if defined(__sun)
#define __EXTENSIONS__ 1
#endif
#include <aio.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define MAX_CLIENTS 64
#define BUFFER_SIZE 4096

struct Client
{
    struct aiocb request;
    unsigned char buffer[BUFFER_SIZE];
    int number;
    int role;
};

static struct Client *clients[MAX_CLIENTS];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finished = PTHREAD_COND_INITIALIZER;
static sigset_t completion_signals;
static sigset_t termination_signals;
static pthread_t main_thread;
static volatile sig_atomic_t stopping = 0;
static int collector_done = 0;

static const char *socket_path;
static int listener = -1;
static int sync_mode = 0;
static int active = 0;
static int next_number = 1;
static int failed = 0;

static int sync_stage = 0;
static size_t sync_offset = 0;
static struct Client *first_client;
static struct Client *second_client;

static const char *const parts[] = {
    NULL, "Hello ", "good day\n", "World\n"};
static const size_t lengths[] = {0, 6, 9, 6};

static void interrupt_accept(int signum)
{
    (void)signum;
}

static void *watch_termination(void *unused)
{
    (void)unused;
    int signum;
    int error = sigwait(&termination_signals, &signum);
    if (error == 0)
    {
        stopping = 1;
        (void)pthread_kill(main_thread, SIGUSR2);
    }
    return NULL;
}

static void abort_sync_locked(void)
{
    failed = 1;
    (void)pthread_kill(main_thread, SIGUSR2);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] != NULL)
            (void)shutdown(clients[i]->request.aio_fildes, SHUT_RDWR);
    }
}

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

static int send_ack(struct Client *client)
{
    const unsigned char ready = 1;

    if (client == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    return write_all(client->request.aio_fildes, &ready, 1);
}

static int sync_input(struct Client *client,
                      const unsigned char *data, size_t length)
{
    size_t i = 0;

    if (client->role == 0)
    {
        if (length == 0)
            return 0;

        int role = data[i++];

        if (role != 1 && role != 2)
        {
            fprintf(stderr, "Invalid sync client role\n");
            return -1;
        }
        if ((role == 1 && first_client != NULL) ||
            (role == 2 && second_client != NULL))
        {
            fprintf(stderr, "Duplicate sync client role\n");
            return -1;
        }

        client->role = role;
        if (role == 1)
            first_client = client;
        else
            second_client = client;

        fprintf(stderr, "Sync client %d registered\n", role);

        if (first_client != NULL && second_client != NULL &&
            sync_stage == 0)
        {
            sync_stage = 1;
            if (send_ack(first_client) == -1)
                return -1;
        }
    }

    for (; i < length; i++)
    {
        int expected_role;

        if (sync_stage == 1 || sync_stage == 3)
            expected_role = 1;
        else if (sync_stage == 2)
            expected_role = 2;
        else
            return -1;

        if (client->role != expected_role)
            return -1;

        unsigned char expected =
            (unsigned char)parts[sync_stage][sync_offset];

        if (data[i] != expected)
        {
            fprintf(stderr, "Unexpected sync text\n");
            return -1;
        }

        unsigned char upper =
            (unsigned char)toupper(data[i]);

        if (write_all(STDOUT_FILENO, &upper, 1) == -1)
            return -1;

        sync_offset++;

        if (sync_offset == lengths[sync_stage])
        {
            sync_offset = 0;

            if (sync_stage == 1)
            {
                sync_stage = 2;
                if (send_ack(second_client) == -1)
                    return -1;
            }
            else if (sync_stage == 2)
            {
                sync_stage = 3;
                if (send_ack(first_client) == -1)
                    return -1;
            }
            else
            {
                sync_stage = 4;
                fprintf(stderr,
                        "Sync demonstration completed\n");
            }
        }
    }

    return 0;
}

static void disconnect_client(int index, int error)
{
    struct Client *client = clients[index];

    if (error)
    {
        failed = 1;
        if (sync_mode)
            abort_sync_locked();
    }

    if (sync_mode && client->role != 0)
    {
        if ((client->role == 1 && sync_stage != 4) ||
            (client->role == 2 && sync_stage < 3))
        {
            fprintf(stderr,
                    "Client disconnected before sync finished\n");
            abort_sync_locked();
        }
    }

    if (sync_mode && client->role == 0 && sync_stage != 4)
        abort_sync_locked();

    fprintf(stderr, "Client %d disconnected\n",
            client->number);

    close(client->request.aio_fildes);

    if (client == first_client)
        first_client = NULL;
    if (client == second_client)
        second_client = NULL;

    clients[index] = NULL;
    free(client);
    active--;

    pthread_cond_broadcast(&finished);
}

static void process_client(int index)
{
    struct Client *client = clients[index];
    int error = aio_error(&client->request);

    if (error == EINPROGRESS)
        return;

    ssize_t count = aio_return(&client->request);

    if (error != 0 || count < 0)
    {
        if (error > 0)
            errno = error;
        perror("aio read");
        disconnect_client(index, 1);
        return;
    }

    if (count == 0)
    {
        disconnect_client(index, 0);
        return;
    }

    if (sync_mode)
    {
        if (sync_input(client, client->buffer,
                       (size_t)count) == -1)
        {
            fprintf(stderr, "Sync protocol or connection error\n");
            disconnect_client(index, 1);
            return;
        }
    }
    else
    {
        for (ssize_t i = 0; i < count; i++)
        {
            client->buffer[i] =
                (unsigned char)toupper(client->buffer[i]);
        }

        if (write_all(STDOUT_FILENO, client->buffer,
                      (size_t)count) == -1)
        {
            perror("write stdout");
            disconnect_client(index, 1);
            return;
        }
    }

    if (aio_read(&client->request) == -1)
    {
        perror("aio_read");
        disconnect_client(index, 1);
    }
}

static void *collect_reads(void *unused)
{
    (void)unused;

    for (;;)
    {
        int signum;
        int error = sigwait(&completion_signals, &signum);

        if (error != 0)
        {
            errno = error;
            perror("sigwait");
            continue;
        }

        pthread_mutex_lock(&mutex);
        if (collector_done)
        {
            pthread_mutex_unlock(&mutex);
            break;
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i] != NULL)
                process_client(i);
        }

        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

static int add_client(int fd)
{
    pthread_mutex_lock(&mutex);

    int index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] == NULL)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        fprintf(stderr, "Too many clients\n");
        close(fd);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    struct Client *client = calloc(1, sizeof(*client));

    if (client == NULL)
    {
        perror("calloc");
        close(fd);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    client->number = next_number++;
    client->request.aio_fildes = fd;
    client->request.aio_buf = client->buffer;
    client->request.aio_nbytes = sizeof(client->buffer);
    client->request.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    client->request.aio_sigevent.sigev_signo = SIGUSR1;

    clients[index] = client;
    active++;

    if (aio_read(&client->request) == -1)
    {
        perror("aio_read");
        clients[index] = NULL;
        active--;
        close(fd);
        free(client);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    fprintf(stderr, "Client %d connected (fd=%d)\n",
            client->number, fd);

    pthread_mutex_unlock(&mutex);
    return 0;
}

static int serve(int listener_fd)
{
    int accepted = 0;
    int accept_error = 0;

    while (!stopping && (!sync_mode || accepted < 2))
    {
        int fd = accept(listener_fd, NULL, NULL);

        if (fd == -1)
        {
            if (errno == EINTR)
            {
                pthread_mutex_lock(&mutex);
                int sync_failed = sync_mode && failed;
                pthread_mutex_unlock(&mutex);
                if (sync_failed)
                {
                    accept_error = 1;
                    break;
                }
                continue;
            }
            if (stopping)
                break;
            perror("accept");
            accept_error = 1;
            break;
        }

        if (add_client(fd) == -1 && sync_mode)
        {
            accept_error = 1;
            break;
        }

        if (sync_mode)
            accepted++;
    }

    pthread_mutex_lock(&mutex);
    if (stopping || accept_error)
    {
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i] != NULL)
                (void)shutdown(clients[i]->request.aio_fildes, SHUT_RDWR);
    }

    while (active > 0)
        pthread_cond_wait(&finished, &mutex);

    int status = (stopping && !accept_error) ||
                         (sync_mode && !failed && !accept_error && sync_stage == 4)
                     ? EXIT_SUCCESS
                     : EXIT_FAILURE;
    pthread_mutex_unlock(&mutex);
    return status;
}

int main(int argc, char *argv[])
{
    struct sockaddr_un address;
    struct sigaction action;
    pthread_t collector;
    pthread_t watcher;
    int status;
    main_thread = pthread_self();

    if (argc > 2 ||
        (argc == 2 && strcmp(argv[1], "--sync") != 0))
    {
        fprintf(stderr, "Usage: %s [--sync]\n", argv[0]);
        return EXIT_FAILURE;
    }

    sync_mode = argc == 2;

    char path[sizeof(address.sun_path)];
    int n = snprintf(path, sizeof(path),
                     "/tmp/task32_%ld.sock", (long)getuid());

    if (n < 0 || (size_t)n >= sizeof(path))
    {
        fprintf(stderr, "Socket path too long\n");
        return EXIT_FAILURE;
    }

    socket_path = path;

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, (size_t)n + 1);

    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (bind(listener, (struct sockaddr *)&address,
             sizeof(address)) == -1)
    {
        perror("bind");
        close(listener);
        return EXIT_FAILURE;
    }

    if (listen(listener, MAX_CLIENTS) == -1)
    {
        perror("listen");
        close(listener);
        unlink(path);
        return EXIT_FAILURE;
    }

    sigemptyset(&completion_signals);
    sigaddset(&completion_signals, SIGUSR1);
    sigemptyset(&termination_signals);
    sigaddset(&termination_signals, SIGINT);
    sigaddset(&termination_signals, SIGTERM);
    sigaddset(&termination_signals, SIGHUP);
    sigset_t blocked = completion_signals;
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGHUP);

    int error = pthread_sigmask(SIG_BLOCK, &blocked, NULL);
    if (error != 0)
    {
        errno = error;
        perror("pthread_sigmask");
        close(listener);
        unlink(path);
        return EXIT_FAILURE;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = interrupt_accept;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGUSR2, &action, NULL) == -1 ||
        signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        close(listener);
        unlink(path);
        return EXIT_FAILURE;
    }

    error = pthread_create(&collector, NULL,
                           collect_reads, NULL);
    if (error != 0)
    {
        errno = error;
        perror("pthread_create");
        close(listener);
        unlink(path);
        return EXIT_FAILURE;
    }

    error = pthread_create(&watcher, NULL, watch_termination, NULL);
    if (error != 0)
    {
        errno = error;
        perror("pthread_create termination");
        pthread_mutex_lock(&mutex);
        collector_done = 1;
        pthread_mutex_unlock(&mutex);
        (void)pthread_kill(collector, SIGUSR1);
        (void)pthread_join(collector, NULL);
        close(listener);
        unlink(path);
        return EXIT_FAILURE;
    }

    printf("Server PID: %ld\n", (long)getpid());
    printf("Socket: %s\n", path);
    printf("Mode: POSIX AIO%s\n",
           sync_mode ? " synchronized" : "");
    printf("Waiting for clients...\n");
    fflush(stdout);

    status = serve(listener);

    pthread_mutex_lock(&mutex);
    collector_done = 1;
    pthread_mutex_unlock(&mutex);
    (void)pthread_kill(collector, SIGUSR1);
    (void)pthread_join(collector, NULL);

    if (!stopping)
        (void)pthread_kill(watcher, SIGTERM);
    (void)pthread_join(watcher, NULL);

    close(listener);
    listener = -1;

    if (unlink(path) == -1)
    {
        perror("unlink");
        status = EXIT_FAILURE;
    }

    return status;
}