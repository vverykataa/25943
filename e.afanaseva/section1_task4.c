#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 1024

struct Node
{
    char *str;
    struct Node *next;
};

int main()
{
    char buffer[MAX_LINE];

    struct Node *head = NULL;
    struct Node *tail = NULL;

    while (1)
    {
        printf("Введите строку: ");

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            break;
        }

        if (buffer[0] == '.')
        {
            break;
        }

        size_t len = strlen(buffer);

        struct Node *new_node = malloc(sizeof(struct Node));

        if (new_node == NULL)
        {
            perror("malloc");
            return 1;
        }

        new_node->str = malloc(len + 1);

        if (new_node->str == NULL)
        {
            perror("malloc");
            free(new_node);
            return 1;
        }

        strcpy(new_node->str, buffer);

        new_node->next = NULL;

        if (head == NULL)
        {
            head = new_node;
            tail = new_node;
        }
        else
        {
            tail->next = new_node;
            tail = new_node;
        }
    }

    printf("\nСписок строк:\n");

    struct Node *current = head;

    while (current != NULL)
    {
        printf("%s", current->str);
        current = current->next;
    }

    current = head;

    while (current != NULL)
    {
        struct Node *next = current->next;

        free(current->str);
        free(current);

        current = next;
    }

    return 0;
}