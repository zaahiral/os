#include "process/syscall.h"

// Adds a line to a file on the disk with the file system calls, creating the file if it does not exist.
extern "C" void _start()
{
    const char line[] = "Written by a user-space program.\n";
    uint64_t fd = syscall(SYSCALL_FOPEN, (uint64_t)"disk/log.txt", FILE_CREATE | FILE_APPEND);
    if (fd == (uint64_t)-1)
    {
        syscall(SYSCALL_DPRINTF, (uint64_t)"Could not open disk/log.txt.\n");
        syscall(SYSCALL_EXIT, 1);
    }
    syscall(SYSCALL_FWRITE, fd, (uint64_t)line, sizeof(line) - 1);
    syscall(SYSCALL_FCLOSE, fd);
    syscall(SYSCALL_DPRINTF, (uint64_t)"Added a line to disk/log.txt.\n");
    syscall(SYSCALL_EXIT, 0);
}
