#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <unistd.h>
#include "cossyscall.h"

/* POSIX functions that need no system call of their own on cOS, which programs such as binutils and GCC use. */

/* cOS does not have file permissions, so changing them succeeds if the file exists. */
int chmod(const char* path, mode_t mode)
{
    struct stat st;
    return stat(path, &st);
}

int fchmod(int fd, mode_t mode)
{
    struct stat st;
    return fstat(fd, &st);
}

mode_t umask(mode_t mask)
{
    return 0;
}

/* Files are not shared with other programs and are never closed on exec, so the only flags are the access mode
   and appending, which can only be set when a file is opened. Reads and writes always block. */
int fcntl(int fd, int command, ...)
{
    struct stat st;
    if (fd < 0 || fstat(fd, &st) < 0)
    {
        errno = EBADF;
        return -1;
    }
    if (command == F_GETFD || command == F_SETFD) { return 0; }
    if (command == F_GETFL) { return O_RDWR; }
    if (command == F_SETFL)
    {
        va_list arguments;
        va_start(arguments, command);
        int flags = va_arg(arguments, int);
        va_end(arguments);
        if (!(flags & (O_NONBLOCK | O_APPEND))) { return 0; }
    }
    errno = EINVAL;
    return -1;
}

long sysconf(int name)
{
    if (name == _SC_PAGESIZE) { return 4096; }
    /* times() counts in milliseconds. */
    if (name == _SC_CLK_TCK) { return 1000; }
    if (name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) { return 1; }
    errno = EINVAL;
    return -1;
}

/* Every filesystem has the same limits, so the path does not matter. */
long pathconf(const char* path, int name)
{
    if (name == _PC_PATH_MAX) { return PATH_MAX; }
    if (name == _PC_NAME_MAX) { return NAME_MAX; }
    errno = EINVAL;
    return -1;
}

/* cOS has no working directory, so every path starts from the root. */
char* getcwd(char* buffer, size_t size)
{
    if (size < 2)
    {
        errno = ERANGE;
        return NULL;
    }
    strcpy(buffer, "/");
    return buffer;
}

char* getwd(char* buffer)
{
    return getcwd(buffer, 2);
}

/* cOS has no links, so a path is made absolute by putting / in front of it, and its . and .. parts are removed.
   libiberty, which binutils and GCC use, needs realpath to find where a program is. */
char* realpath(const char* path, char* resolved)
{
    struct stat st;
    if (stat(path, &st) < 0) { return NULL; }
    char* result = resolved ? resolved : malloc(PATH_MAX);
    if (!result)
    {
        errno = ENOMEM;
        return NULL;
    }
    size_t length = 0;
    const char* input = path;
    while (*input)
    {
        const char* end = input;
        while (*end && *end != '/') { end++; }
        size_t partLength = end - input;
        if (partLength == 2 && input[0] == '.' && input[1] == '.')
        {
            /* Remove the last part, along with the / before it. */
            while (length && result[length - 1] != '/') { length--; }
            if (length) { length--; }
        }
        else if (partLength && !(partLength == 1 && input[0] == '.'))
        {
            if (length + partLength + 2 > PATH_MAX)
            {
                if (!resolved) { free(result); }
                errno = ENAMETOOLONG;
                return NULL;
            }
            result[length++] = '/';
            memcpy(result + length, input, partLength);
            length += partLength;
        }
        input = *end ? end + 1 : end;
    }
    if (!length) { result[length++] = '/'; }
    result[length] = '\0';
    return result;
}

/* The working directory is always the root, so it can only be changed to the root. */
int chdir(const char* path)
{
    if (!strcmp(path, "/") || !strcmp(path, ".")) { return 0; }
    errno = ENOSYS;
    return -1;
}

/* cOS does not keep the times that a file was accessed and changed, so setting them succeeds if the file exists. */
int utimes(const char* path, const struct timeval times[2])
{
    struct stat st;
    return stat(path, &st);
}

DIR* opendir(const char* path)
{
    struct stat st;
    if (stat(path, &st) < 0) { return NULL; }
    if (!S_ISDIR(st.st_mode))
    {
        errno = ENOTDIR;
        return NULL;
    }
    DIR* directory = calloc(1, sizeof(DIR));
    if (!directory)
    {
        errno = ENOMEM;
        return NULL;
    }
    if (strlen(path) >= sizeof(directory->dd_path))
    {
        free(directory);
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(directory->dd_path, path);
    return directory;
}

/* The kernel reads the entry at the directory's index each time, so the directory is read by its path again. */
struct dirent* readdir(DIR* directory)
{
    long type = cos_syscall(SYSCALL_READDIR, (long)directory->dd_path, directory->dd_index, (long)directory->dd_entry.d_name);
    /* 0 is the end of the directory, and -1 means the directory has gone. */
    if (type < 0) { errno = ENOENT; }
    if (type <= 0) { return NULL; }
    directory->dd_index++;
    directory->dd_entry.d_ino = directory->dd_index;
    directory->dd_entry.d_type = type == 2 ? DT_DIR : DT_REG;
    return &directory->dd_entry;
}

int closedir(DIR* directory)
{
    free(directory);
    return 0;
}

void rewinddir(DIR* directory)
{
    directory->dd_index = 0;
}

long telldir(DIR* directory)
{
    return directory->dd_index;
}

void seekdir(DIR* directory, long index)
{
    directory->dd_index = index;
}
