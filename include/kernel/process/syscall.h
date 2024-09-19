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

// Flags for SYSCALL_FOPEN.
#define FILE_CREATE 0x1
#define FILE_APPEND 0x2

uint64_t syscall(uint64_t num)
{
    uint64_t ret;
    asm volatile("mov %1, %%rax\n"
                    "syscall\n"
                    "mov %%rax, %0\n" : "=r"(ret) : "r" (num));
    return ret;
}

uint64_t syscall(uint64_t num, uint64_t arg1)
{
    uint64_t ret;
    asm volatile("mov %1, %%rax\n" "mov %2, %%rdi\n"
                    "syscall\n"
                    "mov %%rax, %0\n" : "=r"(ret) : "r" (num), "r" (arg1): "rdi");
    return ret;
}

uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2)
{
    uint64_t ret;
    asm volatile("mov %1, %%rax\n" "mov %2, %%rdi\n" "mov %3, %%rsi\n"
                    "syscall\n"
                    "mov %%rax, %0\n" : "=r"(ret) : "r" (num), "r" (arg1), "r" (arg2): "rdi", "rsi");
    return ret;
}

uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    uint64_t ret;
    asm volatile("mov %1, %%rax\n" "mov %2, %%rdi\n" "mov %3, %%rsi\n" "mov %4, %%rdx\n"
                    "syscall\n"
                    "mov %%rax, %0\n" : "=r"(ret) : "r" (num), "r" (arg1), "r" (arg2), "r" (arg3) : "rdi", "rsi", "rdx");
    return ret;
}

uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
    uint64_t ret;
    asm volatile("mov %1, %%rax\n" "mov %2, %%rdi\n" "mov %3, %%rsi\n" "mov %4, %%rdx\n" "mov %5, %%r10\n"
                    "syscall\n"
                    "mov %%rax, %0\n" : "=r"(ret) : "r" (num), "r" (arg1), "r" (arg2), "r" (arg3), "r" (arg4) : "rdi", "rsi", "rdx", "r10");
    return ret;
}

uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    uint64_t ret;
    asm volatile("mov %1, %%rax\n" "mov %2, %%rdi\n" "mov %3, %%rsi\n" "mov %4, %%rdx\n" "mov %5, %%r10\n" "mov %6, %%r8\n"
                    "syscall\n"
                    "mov %%rax, %0\n" : "=r"(ret) : "r" (num), "r" (arg1), "r" (arg2), "r" (arg3), "r" (arg4), "r" (arg5): "rdi", "rsi", "rdx", "r10", "r8");
    return ret;
}

uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    uint64_t ret;
    asm volatile("mov %1, %%rax\n" "mov %2, %%rdi\n" "mov %3, %%rsi\n" "mov %4, %%rdx\n" "mov %5, %%r10\n" "mov %6, %%r8\n" "mov %7, %%r9\n"
                    "syscall\n"
                    "mov %%rax, %0\n" : "=r"(ret) : "r" (num), "r" (arg1), "r" (arg2), "r" (arg3), "r" (arg4), "r" (arg5), "r" (arg6): "rdi", "rsi", "rdx", "r10", "r8", "r9");
    return ret;
}

#endif
