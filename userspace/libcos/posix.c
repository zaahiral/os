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
   and appending, which is only set when a file is opened. Reads and writes always block. */
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
        if (!(flags & O_NONBLOCK)) { return 0; }
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
