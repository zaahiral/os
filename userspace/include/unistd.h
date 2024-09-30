#ifndef UNISTD_H
#define UNISTD_H

#include "process/syscall.h"

// Wrappers for the file and process system calls. Each returns -1 if the system call fails.

int64_t open(const char* path, uint64_t flags = 0) { return syscall(SYSCALL_FOPEN, (uint64_t)path, flags); }
int64_t read(int64_t fd, void* buffer, uint64_t count) { return syscall(SYSCALL_FREAD, fd, (uint64_t)buffer, count); }
int64_t write(int64_t fd, const void* buffer, uint64_t count) { return syscall(SYSCALL_FWRITE, fd, (uint64_t)buffer, count); }
int64_t close(int64_t fd) { return syscall(SYSCALL_FCLOSE, fd); }

// List the entry at an index in a directory. Returns 1 for a file, 2 for a directory, or 0 past the last entry.
int64_t readdir(const char* path, uint64_t index, char* name) { return syscall(SYSCALL_READDIR, (uint64_t)path, index, (uint64_t)name); }

// Start a program with a string of arguments separated by spaces, and return its task ID.
int64_t spawn(const char* path, const char* arguments = nullptr) { return syscall(SYSCALL_SPAWN, (uint64_t)path, (uint64_t)arguments, 0); }
// Wait for a program to exit, and return its exit code.
int64_t wait(int64_t tid) { return syscall(SYSCALL_WAIT, tid); }

// Move the program break by an amount, and return the old break, or -1 if the heap cannot grow.
int64_t sbrk(int64_t increment)
{
    uint64_t oldBreak = syscall(SYSCALL_BRK, 0);
    if (!increment) { return oldBreak; }
    if (syscall(SYSCALL_BRK, oldBreak + increment) != oldBreak + increment) { return -1; }
    return oldBreak;
}

#endif
