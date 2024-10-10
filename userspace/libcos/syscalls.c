#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include "cossyscall.h"

/* The operating system functions that newlib calls, built on the cOS system calls.
   The console, files and pipes all use the kernel's descriptor table. */

void _exit(int code)
{
    cos_syscall(SYSCALL_EXIT, code, 0, 0);
    /* The exit system call only returns on another system, such as when a configure script runs a test program on
       the machine that builds it, so the program stops there instead of running forever. */
    __builtin_trap();
}

int open(const char* path, int flags, ...)
{
    long cosFlags = 0;
    if (flags & O_CREAT) { cosFlags |= FILE_CREATE; }
    if (flags & O_APPEND) { cosFlags |= FILE_APPEND; }
    if (flags & O_TRUNC) { cosFlags |= FILE_TRUNCATE; }
    if (flags & O_EXCL) { cosFlags |= FILE_EXCLUSIVE; }
    long fd = cos_syscall(SYSCALL_FOPEN, (long)path, cosFlags, 0);
    if (fd < 0)
    {
        struct stat st;
        errno = fd == -2 ? EMFILE : ((flags & O_EXCL) && !stat(path, &st)) ? EEXIST : ENOENT;
        return -1;
    }
    return fd;
}

int close(int fd)
{
    if (cos_syscall(SYSCALL_FCLOSE, fd, 0, 0) < 0)
    {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int read(int fd, void* buffer, size_t count)
{
    if (count > INT_MAX) { count = INT_MAX; }
    long result = cos_syscall(SYSCALL_FREAD, fd, (long)buffer, count);
    if (result < 0)
    {
        errno = EBADF;
        return -1;
    }
    return result;
}

int write(int fd, const void* buffer, size_t count)
{
    if (count > INT_MAX) { count = INT_MAX; }
    long result = cos_syscall(SYSCALL_FWRITE, fd, (long)buffer, count);
    if (result < 0)
    {
        errno = result == -2 ? EPIPE : EBADF;
        return -1;
    }
    return result;
}

off_t lseek(int fd, off_t offset, int whence)
{
    struct stat st;
    if (fstat(fd, &st) < 0) { return -1; }
    if (S_ISCHR(st.st_mode) || S_ISFIFO(st.st_mode)) { errno = ESPIPE; return -1; }
    long result = cos_syscall(SYSCALL_LSEEK, fd, offset, whence);
    if (result < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return result;
}

static void to_stat(struct FileStatus* status, struct stat* st)
{
    /* cOS does not keep owners, permissions or times, so they are left as zero. */
    memset(st, 0, sizeof(*st));
    st->st_ino = status->inode;
    st->st_nlink = 1;
    st->st_size = status->size;
    st->st_mode = status->kind == 2 ? S_IFCHR : status->kind == 3 ? S_IFIFO : status->kind == 1 ? S_IFDIR : S_IFREG;
    st->st_blksize = 512;
}

int fstat(int fd, struct stat* st)
{
    struct FileStatus status;
    if (cos_syscall(SYSCALL_FSTAT, fd, (long)&status, 0) < 0)
    {
        errno = EBADF;
        return -1;
    }
    to_stat(&status, st);
    return 0;
}

int stat(const char* path, struct stat* st)
{
    struct FileStatus status;
    if (cos_syscall(SYSCALL_STAT, (long)path, (long)&status, 0) < 0)
    {
        errno = ENOENT;
        return -1;
    }
    to_stat(&status, st);
    return 0;
}

/* Duplicates share the kernel open file, including its offset or pipe endpoint. */
int dup(int fd)
{
    long newFd = cos_syscall(SYSCALL_DUP, fd, 0, 0);
    if (newFd < 0)
    {
        errno = newFd == -2 ? EMFILE : EBADF;
        return -1;
    }
    return newFd;
}

/* cOS has no symbolic links, so lstat is the same as stat. */
int lstat(const char* path, struct stat* st)
{
    return stat(path, st);
}

int isatty(int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0) { return 0; }
    if (S_ISCHR(st.st_mode)) { return 1; }
    errno = ENOTTY;
    return 0;
}

void* sbrk(ptrdiff_t increment)
{
    long oldBreak = cos_syscall(SYSCALL_BRK, 0, 0, 0);
    if (increment && cos_syscall(SYSCALL_BRK, oldBreak + increment, 0, 0) != oldBreak + increment)
    {
        errno = ENOMEM;
        return (void*)-1;
    }
    return (void*)oldBreak;
}

int getpid(void)
{
    return cos_syscall(SYSCALL_GETTID, 0, 0, 0);
}

/* cOS has no real-time clock driver yet, so time is counted from boot. */
int gettimeofday(struct timeval* time, void* timezone)
{
    long milliseconds = cos_syscall(SYSCALL_UPTIME, 0, 0, 0);
    time->tv_sec = milliseconds / 1000;
    time->tv_usec = (milliseconds % 1000) * 1000;
    return 0;
}

clock_t times(struct tms* buffer)
{
    long milliseconds = cos_syscall(SYSCALL_UPTIME, 0, 0, 0);
    buffer->tms_utime = milliseconds;
    buffer->tms_stime = 0;
    buffer->tms_cutime = 0;
    buffer->tms_cstime = 0;
    return milliseconds;
}

/* cOS does not have signals, links, fork or exec yet. Programs are started with posix_spawn instead. */
int kill(int pid, int signal)
{
    errno = EINVAL;
    return -1;
}

int link(const char* oldPath, const char* newPath)
{
    errno = EMLINK;
    return -1;
}


int pipe(int fds[2])
{
    if (cos_syscall(SYSCALL_PIPE, (long)fds, 0, 0) < 0)
    {
        errno = EMFILE;
        return -1;
    }
    return 0;
}

int dup2(int fd, int newFd)
{
    long result = cos_syscall(SYSCALL_DUP2, fd, newFd, 0);
    if (result < 0) { errno = EBADF; return -1; }
    return result;
}

int fork(void)
{
    errno = ENOSYS;
    return -1;
}

int execve(const char* path, char* const argv[], char* const envp[])
{
    errno = ENOSYS;
    return -1;
}

int execvp(const char* file, char* const argv[])
{
    errno = ENOSYS;
    return -1;
}

/* The kernel sleeps in milliseconds. */
unsigned int sleep(unsigned int seconds)
{
    cos_syscall(SYSCALL_SLEEP_FOR, seconds * 1000L, 0, 0);
    return 0;
}

/* Without signals, an alarm could not interrupt the program, so none is set. */
unsigned int alarm(unsigned int seconds)
{
    return 0;
}

int wait(int* status)
{
    errno = ECHILD;
    return -1;
}

/* Every file can be read, written and run, so a file can be accessed if it exists. */
int access(const char* path, int mode)
{
    struct stat st;
    return stat(path, &st);
}

int unlink(const char* path)
{
    struct stat st;
    if (stat(path, &st) < 0) { return -1; }
    if (S_ISDIR(st.st_mode))
    {
        errno = EISDIR;
        return -1;
    }
    if (cos_syscall(SYSCALL_UNLINK, (long)path, 0, 0) < 0)
    {
        errno = EROFS;
        return -1;
    }
    return 0;
}

int rmdir(const char* path)
{
    struct stat st;
    if (stat(path, &st) < 0) { return -1; }
    if (!S_ISDIR(st.st_mode))
    {
        errno = ENOTDIR;
        return -1;
    }
    if (cos_syscall(SYSCALL_UNLINK, (long)path, 0, 0) < 0)
    {
        errno = ENOTEMPTY;
        return -1;
    }
    return 0;
}

/* cOS does not have file permissions, so the mode is not used. */
int mkdir(const char* path, mode_t mode)
{
    if (cos_syscall(SYSCALL_MKDIR, (long)path, 0, 0) < 0)
    {
        struct stat st;
        errno = stat(path, &st) ? ENOENT : EEXIST;
        return -1;
    }
    return 0;
}