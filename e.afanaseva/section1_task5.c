#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_LINES 100
#define BUFFER_SIZE 1024

struct Line
{
    off_t offset;
    size_t length;
};

int main(int argc, char *argv[])
{
    int fd;
    char buffer[BUFFER_SIZE];
    struct Line lines[MAX_LINES];

    int line_count = 0;
    off_t line_start = 0;
    ssize_t bytes_read;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s filename\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDONLY);

    if (fd == -1)
    {
        perror("open");
        return 1;
    }

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0)
    {
        for (ssize_t i = 0; i < bytes_read; i++)
        {
            if (buffer[i] == '\n')
            {
                if (line_count >= MAX_LINES)
                {
                    fprintf(stderr, "Too many lines\n");
                    close(fd);
                    return 1;
                }

                lines[line_count].offset = line_start;
                lines[line_count].length =
                    (off_t)lseek(fd, 0L, SEEK_CUR) - bytes_read + i + 1 - line_start;

                line_start =
                    (off_t)lseek(fd, 0L, SEEK_CUR) - bytes_read + i + 1;

                line_count++;
            }
        }
    }

    if (bytes_read == -1)
    {
        perror("read");
        close(fd);
        return 1;
    }

    if (line_start < lseek(fd, 0L, SEEK_CUR))
    {
        if (line_count >= MAX_LINES)
        {
            fprintf(stderr, "Too many lines\n");
            close(fd);
            return 1;
        }

        lines[line_count].offset = line_start;

        lines[line_count].length =
            lseek(fd, 0L, SEEK_CUR) - line_start;

        line_count++;
    }

    printf("Таблица строк:\n");
    printf("№\tОтступ\tДлина\n");

    for (int i = 0; i < line_count; i++)
    {
        printf("%d\t%ld\t%lu\n",
               i + 1,
               (long)lines[i].offset,
               (unsigned long)lines[i].length);
    }

    while (1)
    {
        int number;

        printf("\nВведите номер строки (0 - выход): ");

        if (scanf("%d", &number) != 1)
        {
            fprintf(stderr, "Некорректный номер строки\n");
            break;
        }

        if (number == 0)
        {
            break;
        }

        if (number < 1 || number > line_count)
        {
            printf("Такой строки нет\n");
            continue;
        }

        if (lseek(fd, lines[number - 1].offset, SEEK_SET) == -1)
        {
            perror("lseek");
            close(fd);
            return 1;
        }

        ssize_t n = read(fd, buffer, lines[number - 1].length);

        if (n == -1)
        {
            perror("read");
            close(fd);
            return 1;
        }

        printf("Строка %d: ", number);
        write(STDOUT_FILENO, buffer, n);
    }

    if (close(fd) == -1)
    {
        perror("close");
        return 1;
    }

    return 0;
}