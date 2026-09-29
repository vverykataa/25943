#include <sys/types.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

int main()
{
    time_t now;
    struct tm *sp;

    (void)time(&now);

    /* Калифорния зимой: PST = UTC - 8 часов */
    now -= 8 * 60 * 60;

    sp = gmtime(&now);

    printf("%d/%d/%d %d:%02d PST\n",
           sp->tm_mon + 1,
           sp->tm_mday,
           sp->tm_year + 1900,
           sp->tm_hour,
           sp->tm_min);

    exit(0);
}