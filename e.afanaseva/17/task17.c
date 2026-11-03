#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#define WIDTH 40
#define MAX_LINE 4096

static struct termios original_terminal;
static int terminal_changed = 0;

static void restore_terminal(void)
{
    if (terminal_changed)
        tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal);
}

static void beep(void)
{
    putchar('\a');
    fflush(stdout);
}

static void show_line(const char *line, size_t length,
                      size_t *previous_rows)
{
    char output[MAX_LINE * 2];
    size_t used = 0;
    size_t column = 0;
    size_t rows = 0;
    size_t i = 0;

    while (i < length)
    {
        size_t spaces = 0;
        size_t word_start;
        size_t word_length;

        while (i < length && line[i] == ' ')
        {
            spaces++;
            i++;
        }

        word_start = i;

        while (i < length && line[i] != ' ')
            i++;

        word_length = i - word_start;

        if (word_length > 0 &&
            column > 0 &&
            column + spaces + word_length > WIDTH)
        {
            output[used++] = '\n';
            column = 0;
            rows++;
            spaces = 0;
        }

        while (spaces > 0)
        {
            if (column == WIDTH)
            {
                output[used++] = '\n';
                column = 0;
                rows++;
            }

            output[used++] = ' ';
            column++;
            spaces--;
        }

        for (size_t j = 0; j < word_length; j++)
        {
            if (column == WIDTH)
            {
                output[used++] = '\n';
                column = 0;
                rows++;
            }

            output[used++] = line[word_start + j];
            column++;
        }
    }

    output[used] = '\0';

    if (*previous_rows > 0)
        printf("\033[%zuA", *previous_rows);

    printf("\r\033[J");
    fwrite(output, 1, used, stdout);
    fflush(stdout);

    *previous_rows = rows;
}

int main(void)
{
    struct termios terminal;
    char line[MAX_LINE];
    size_t length = 0;
    size_t previous_rows = 0;
    unsigned char ch;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    {
        fprintf(stderr, "Run this program in a terminal\n");
        return EXIT_FAILURE;
    }

    if (tcgetattr(STDIN_FILENO, &original_terminal) == -1)
    {
        perror("tcgetattr");
        return EXIT_FAILURE;
    }

    terminal = original_terminal;

    terminal.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    terminal.c_iflag &= ~(IXON | ICRNL);

    terminal.c_cc[VMIN] = 1;
    terminal.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) == -1)
    {
        perror("tcsetattr");
        return EXIT_FAILURE;
    }

    terminal_changed = 1;
    atexit(restore_terminal);

    printf("Line editor (40 columns)\n");
    printf("ERASE: Backspace, KILL: Ctrl-U\n");
    printf("Ctrl-W: erase word, Ctrl-D: exit\n\n");
    fflush(stdout);

    while (1)
    {
        ssize_t n = read(STDIN_FILENO, &ch, 1);

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            perror("read");
            break;
        }

        if (n == 0)
            break;

        if (ch == 4)
        {
            if (length == 0)
                break;

            beep();
        }
        else if (ch == original_terminal.c_cc[VERASE] ||
                 ch == 127 || ch == 8)
        {
            if (length > 0)
            {
                length--;
                show_line(line, length, &previous_rows);
            }
            else
                beep();
        }
        else if (ch == original_terminal.c_cc[VKILL])
        {
            length = 0;
            show_line(line, length, &previous_rows);
        }
        else if (ch == 23)
        {
            while (length > 0 && line[length - 1] == ' ')
                length--;

            while (length > 0 && line[length - 1] != ' ')
                length--;

            show_line(line, length, &previous_rows);
        }
        else if (ch == '\r' || ch == '\n')
        {
            putchar('\n');
            fflush(stdout);

            length = 0;
            previous_rows = 0;
        }
        else if (ch >= 32 && ch <= 126)
        {
            if (length >= MAX_LINE - 1)
            {
                beep();
                continue;
            }

            line[length++] = (char)ch;
            show_line(line, length, &previous_rows);
        }
        else
        {
            beep();
        }
    }

    putchar('\n');
    fflush(stdout);

    return EXIT_SUCCESS;
}