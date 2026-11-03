#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <unistd.h>

static void get_permissions(mode_t mode, char permissions[11])
{
    permissions[0] = S_ISDIR(mode) ? 'd' : S_ISREG(mode) ? '-'
                                                         : '?';

    permissions[1] = (mode & S_IRUSR) ? 'r' : '-';
    permissions[2] = (mode & S_IWUSR) ? 'w' : '-';
    permissions[3] = (mode & S_IXUSR) ? 'x' : '-';

    permissions[4] = (mode & S_IRGRP) ? 'r' : '-';
    permissions[5] = (mode & S_IWGRP) ? 'w' : '-';
    permissions[6] = (mode & S_IXGRP) ? 'x' : '-';

    permissions[7] = (mode & S_IROTH) ? 'r' : '-';
    permissions[8] = (mode & S_IWOTH) ? 'w' : '-';
    permissions[9] = (mode & S_IXOTH) ? 'x' : '-';

    permissions[10] = '\0';
}

static const char *get_filename(const char *path)
{
    const char *end = path + strlen(path);
    const char *start;

    while (end > path + 1 && end[-1] == '/')
        end--;

    start = end;

    while (start > path && start[-1] != '/')
        start--;

    if (start == end)
        return "/";

    static char filename[4096];
    size_t length = (size_t)(end - start);

    if (length >= sizeof(filename))
        length = sizeof(filename) - 1;

    memcpy(filename, start, length);
    filename[length] = '\0';

    return filename;
}

static int print_file_info(const char *path)
{
    struct stat st;
    struct passwd *pw;
    struct group *gr;

    char permissions[11];
    char owner[64];
    char group[64];
    char size[32];
    char date[32];

    struct tm tm_info;

    if (lstat(path, &st) == -1)
    {
        perror(path);
        return -1;
    }

    get_permissions(st.st_mode, permissions);

    pw = getpwuid(st.st_uid);

    if (pw != NULL)
        snprintf(owner, sizeof(owner), "%s", pw->pw_name);
    else
        snprintf(owner, sizeof(owner), "%lu",
                 (unsigned long)st.st_uid);

    gr = getgrgid(st.st_gid);

    if (gr != NULL)
        snprintf(group, sizeof(group), "%s", gr->gr_name);
    else
        snprintf(group, sizeof(group), "%lu",
                 (unsigned long)st.st_gid);

    if (S_ISREG(st.st_mode))
    {
        snprintf(size, sizeof(size), "%lld",
                 (long long)st.st_size);
    }
    else
    {
        size[0] = '\0';
    }

    if (localtime_r(&st.st_mtime, &tm_info) == NULL)
    {
        snprintf(date, sizeof(date), "unknown");
    }
    else
    {
        strftime(date, sizeof(date),
                 "%Y-%m-%d %H:%M", &tm_info);
    }

    printf("%-10s %5lu %-12s %-12s %10s %-16s %s\n",
           permissions,
           (unsigned long)st.st_nlink,
           owner,
           group,
           size,
           date,
           get_filename(path));

    return 0;
}

int main(int argc, char *argv[])
{
    int error = 0;

    printf("%-10s %5s %-12s %-12s %10s %-16s %s\n",
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