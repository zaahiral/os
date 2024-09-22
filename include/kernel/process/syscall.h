#ifndef SYSCALL_H
#define SYSCALL_H
#include "types.h"

#define SYSCALL_DPRINTF 1
#define SYSCALL_VMMAP 2
#define SYSCALL_VMUNMAP 3
#define SYSCALL_FOPEN 4
#define SYSCALL_FCLOSE 5
#define SYSCALL_FREAD 6
#define SYSCALL_FWRITE 7
#define SYSCALL_EREGISTER 8
#define SYSCALL_ELISTEN 9
#define SYSCALL_EDEREGISTER 10
#define SYSCALL_EREAD 11
#define SYSCALL_SLEEP_FOR 12
#define SYSCALL_GETTID 13
#define SYSCALL_EXIT 14
#define SYSCALL_SPAWN 15
#define SYSCALL_WAIT 16
#define SYSCALL_BRK 17
#define SYSCALL_READDIR 18
#define SYSCALL_CLEAR 19

// Flags for SYSCALL_FOPEN.
#define FILE_CREATE 0x1
#define FILE_APPEND 0x2

// The system call number goes in rax and the arguments in rdi, rsi, rdx, r10, r8 and r9. The syscall instruction
// overwrites rcx and r11, and the kernel may read or write memory, so the compiler is told about both.

inline uint64_t syscall(uint64_t num)
{
    uint64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

inline uint64_t syscall(uint64_t num, uint64_t arg1)
{
    uint64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1) : "rcx", "r11", "memory");
    return ret;
}

inline uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2)
{
    uint64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2) : "rcx", "r11", "memory");
    return ret;
}

inline uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    uint64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return ret;
}

// There are no constraint letters for r10, r8 and r9, so these arguments are placed in register variables.
inline uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

inline uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8 asm("r8") = arg5;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

inline uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8 asm("r8") = arg5;
    register uint64_t r9 asm("r9") = arg6;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

#endif
