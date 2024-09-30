#ifndef COSSYSCALL_H
#define COSSYSCALL_H

#include "process/syscallnumbers.h"

/* Make a cOS system call. The kernel's sysret returns through rcx and r11, so they are clobbered. */
static inline long cos_syscall(long number, long arg1, long arg2, long arg3)
{
    long result;
    __asm__ volatile("syscall" : "=a"(result) : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return result;
}

#endif
