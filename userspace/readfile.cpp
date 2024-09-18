#include "process/syscall.h"

// Reads a file from the initrd with the file system calls, and prints it.
extern "C" void _start()
{
    char buffer[64];
    uint64_t fd = syscall(SYSCALL_FOPEN, (uint64_t)"initrd/hello.txt", 0);
    uint64_t length = syscall(SYSCALL_FREAD, fd, (uint64_t)buffer, sizeof(buffer) - 1);
    buffer[length] = '\0';
    syscall(SYSCALL_FCLOSE, fd);

    syscall(SYSCALL_DPRINTF, (uint64_t)"Read from initrd/hello.txt: ");
    syscall(SYSCALL_DPRINTF, (uint64_t)buffer);
    // The exit code is the number of bytes read.
    syscall(SYSCALL_EXIT, length);
}
