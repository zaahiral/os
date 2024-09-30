#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include "cossyscall.h"

/* The operating system functions that newlib calls, built on the cOS system calls.
   File descriptors 0, 1 and 2 are the keyboard and the screen, and the kernel's file descriptors start after them. */

#define CONSOLE_DESCRIPTORS 3

void _exit(int code)
{
    cos_syscall(SYSCALL_EXIT, code, 0, 0);
    while (1);
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
        errno = ((flags & O_EXCL) && !stat(path, &st)) ? EEXIST : ENOENT;
        return -1;
    }
    return fd + CONSOLE_DESCRIPTORS;
}

int close(int fd)
{
    if (fd < CONSOLE_DESCRIPTORS) { return 0; }
    if (cos_syscall(SYSCALL_FCLOSE, fd - CONSOLE_DESCRIPTORS, 0, 0) < 0)
    {
        errno = EBADF;
        return -1;
    }
    return 0;
}

/* Scan code set 1 on a US keyboard, indexed by scan code. */
static const char lowerCase[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static const char upperCase[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

/* Read a line from the keyboard, echoing it as it is typed, and return it with its newline. */
static int read_keyboard_line(char* buffer, int count)
{
    static long keyboardEventDescriptor = -1;
    if (keyboardEventDescriptor < 0) { keyboardEventDescriptor = cos_syscall(SYSCALL_EREGISTER, (long)"HID/Keyboard", 0, 0); }
    int length = 0;
    int isShiftHeld = 0;
    while (1)
    {
        cos_syscall(SYSCALL_ELISTEN, keyboardEventDescriptor, 0, 0);
        long scanCode;
        while ((scanCode = cos_syscall(SYSCALL_EREAD, keyboardEventDescriptor, 0, 0)))
        {
            if (scanCode == 0x2A || scanCode == 0x36) { isShiftHeld = 1; continue; }
            if (scanCode == 0xAA || scanCode == 0xB6) { isShiftHeld = 0; continue; }
            if (scanCode & 0x80) { continue; }
            if (scanCode == 0x1C)
            {
                buffer[length++] = '\n';
                cos_syscall(SYSCALL_CONSOLE_WRITE, (long)"\n", 1, 0);
                return length;
            }
            if (scanCode == 0x0E)
            {
                if (length) { length--; cos_syscall(SYSCALL_CONSOLE_WRITE, (long)"\b", 1, 0); }
                continue;
            }
            if (scanCode >= (long)sizeof(lowerCase) - 1) { continue; }
            char c = isShiftHeld ? upperCase[scanCode] : lowerCase[scanCode];
            /* One byte is kept free for the newline. */
            if (!c || length == count - 1) { continue; }
            buffer[length++] = c;
            cos_syscall(SYSCALL_CONSOLE_WRITE, (long)&c, 1, 0);
        }
    }
}

int read(int fd, void* buffer, int count)
{
    if (fd == 0) { return read_keyboard_line(buffer, count); }
    if (fd < CONSOLE_DESCRIPTORS)
    {
        errno = EBADF;
        return -1;
    }
    long result = cos_syscall(SYSCALL_FREAD, fd - CONSOLE_DESCRIPTORS, (long)buffer, count);
    if (result < 0)
    {
        errno = EBADF;
        return -1;
    }
    return result;
}

int write(int fd, const void* buffer, int count)
{
    if (fd == 1 || fd == 2) { return cos_syscall(SYSCALL_CONSOLE_WRITE, (long)buffer, count, 0); }
    if (fd < CONSOLE_DESCRIPTORS)
    {
        errno = EBADF;
        return -1;
    }
    long result = cos_syscall(SYSCALL_FWRITE, fd - CONSOLE_DESCRIPTORS, (long)buffer, count);
    if (result < 0)
    {
        errno = EBADF;
        return -1;
    }
    return result;
}

int lseek(int fd, int offset, int whence)
{
    if (fd < CONSOLE_DESCRIPTORS)
    {
        errno = ESPIPE;
        return -1;
    }
    long result = cos_syscall(SYSCALL_LSEEK, fd - CONSOLE_DESCRIPTORS, offset, whence);
    if (result < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return result;
}

static void to_stat(struct FileStatus* status, struct stat* st)
{
    st->st_size = status->size;
    st->st_mode = status->isDirectory ? S_IFDIR : S_IFREG;
    st->st_blksize = 512;
}

int fstat(int fd, struct stat* st)
{
    if (fd < CONSOLE_DESCRIPTORS)
    {
        st->st_mode = S_IFCHR;
        return 0;
    }
    struct FileStatus status;
    if (cos_syscall(SYSCALL_FSTAT, fd - CONSOLE_DESCRIPTORS, (long)&status, 0) < 0)
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

int isatty(int fd)
{
    if (fd < CONSOLE_DESCRIPTORS) { return 1; }
    errno = ENOTTY;
    return 0;
}

void* sbrk(int increment)
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