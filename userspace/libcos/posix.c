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
