#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static void get_permissions(mode_t mode, char permissions[11])
{
    if (S_ISDIR(mode))
        permissions[0] = 'd';
    else if (S_ISREG(mode))
        permissions[0] = '-';
    else if (S_ISLNK(mode))
        permissions[0] = 'l';
    else if (S_ISCHR(mode))
        permissions[0] = 'c';
    else if (S_ISBLK(mode))
        permissions[0] = 'b';
    else if (S_ISFIFO(mode))
        permissions[0] = 'p';
    else if (S_ISSOCK(mode))
        permissions[0] = 's';
    else
        permissions[0] = '?';

    permissions[1] = (mode & S_IRUSR) ? 'r' : '-';
    permissions[2] = (mode & S_IWUSR) ? 'w' : '-';
    permissions[3] = (mode & S_IXUSR) ? 'x' : '-';

    permissions[4] = (mode & S_IRGRP) ? 'r' : '-';
    permissions[5] = (mode & S_IWGRP) ? 'w' : '-';
    permissions[6] = (mode & S_IXGRP) ? 'x' : '-';

    permissions[7] = (mode & S_IROTH) ? 'r' : '-';
    permissions[8] = (mode & S_IWOTH) ? 'w' : '-';
    permissions[9] = (mode & S_IXOTH) ? 'x' : '-';

    if (mode & S_ISUID)
        permissions[3] = (mode & S_IXUSR) ? 's' : 'S';

    if (mode & S_ISGID)
        permissions[6] = (mode & S_IXGRP) ? 's' : 'S';

    if (mode & S_ISVTX)
        permissions[9] = (mode & S_IXOTH) ? 't' : 'T';

    permissions[10] = '\0';
}

static void get_owner(uid_t uid, char *owner, size_t size)
{
    struct passwd *pw = getpwuid(uid);

    if (pw != NULL)
        snprintf(owner, size, "%s", pw->pw_name);
    else
        snprintf(owner, size, "%" PRIuMAX, (uintmax_t)uid);
}

static void get_group(gid_t gid, char *group, size_t size)
{
    struct group *gr = getgrgid(gid);

    if (gr != NULL)
        snprintf(group, size, "%s", gr->gr_name);
    else
        snprintf(group, size, "%" PRIuMAX, (uintmax_t)gid);
}

static void get_size(const struct stat *st, char *size,
                     size_t size_length)
{
    if (S_ISREG(st->st_mode))
        snprintf(size, size_length, "%" PRIdMAX,
                 (intmax_t)st->st_size);
    else
        size[0] = '\0';
}

static void get_date(time_t mtime, char *date, size_t size)
{
    struct tm tm_info;

    if (localtime_r(&mtime, &tm_info) == NULL ||
        strftime(date, size, "%Y-%m-%d %H:%M", &tm_info) == 0)
    {
        snprintf(date, size, "unknown");
    }
}

static void get_filename(const char *path,
                         const char **start, size_t *length)
{
    size_t end = strlen(path);

    while (end > 0 && path[end - 1] == '/')
        end--;

    if (end == 0)
    {
        *start = path;
        *length = strlen(path) > 0 ? 1 : 0;
        return;
    }

    size_t begin = end;

    while (begin > 0 && path[begin - 1] != '/')
        begin--;

    *start = path + begin;
    *length = end - begin;
}

static int print_file_info(const char *path)
{
    struct stat st;

    char permissions[11];
    char owner[64];
    char group[64];
    char size[32];
    char date[32];

    const char *filename;
    size_t filename_length;

    if (lstat(path, &st) == -1)
    {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }

    get_permissions(st.st_mode, permissions);
    get_owner(st.st_uid, owner, sizeof(owner));
    get_group(st.st_gid, group, sizeof(group));
    get_size(&st, size, sizeof(size));
    get_date(st.st_mtime, date, sizeof(date));
    get_filename(path, &filename, &filename_length);

    printf("%-11s %5" PRIuMAX " %-12s %-12s %10s %-16s ",
           permissions,
           (uintmax_t)st.st_nlink,
           owner,
           group,
           size,
           date);

    fwrite(filename, 1, filename_length, stdout);
    putchar('\n');

    return 0;
}

int main(int argc, char *argv[])
{
    int error = 0;

    printf("%-11s %5s %-12s %-12s %10s %-16s %s\n",
           "Permissions",
           "Links",
           "Owner",
           "Group",
           "Size",
           "Modified",
           "Name");

    if (argc == 1)
    {
        if (print_file_info(".") == -1)
            error = 1;
    }
    else
    {
        for (int i = 1; i < argc; i++)
        {
            if (print_file_info(argv[i]) == -1)
                error = 1;
        }
    }

    return error ? EXIT_FAILURE : EXIT_SUCCESS;
}