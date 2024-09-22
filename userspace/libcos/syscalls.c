#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include "process/syscallnumbers.h"

/* The operating system functions that newlib calls, built on the cOS system calls.
   File descriptors 0, 1 and 2 are the keyboard and the screen, and the kernel's file descriptors start after them. */

#define CONSOLE_DESCRIPTORS 3

static long cos_syscall(long number, long arg1, long arg2, long arg3)
{
    long result;
    __asm__ volatile("syscall" : "=a"(result) : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return result;
}

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
    long fd = cos_syscall(SYSCALL_FOPEN, (long)path, cosFlags, 0);
    if (fd < 0)
    {
        errno = ENOENT;
        return -1;
    }
