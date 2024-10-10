#include "start.h"
#include "stdio.h"

// Adds a line to a file on the disk with the file system calls, creating the file if it does not exist.
int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "disk/log.txt";
    const char line[] = "Written by a user-space program.\n";
    int64_t fd = syscall(SYSCALL_FOPEN, (uint64_t)path, FILE_CREATE | FILE_APPEND);
    if (fd < 0)
    {
        printf("Could not open %s.\n", path);
        return 1;
    }
    syscall(SYSCALL_FWRITE, fd, (uint64_t)line, sizeof(line) - 1);
    syscall(SYSCALL_FCLOSE, fd);
    printf("Added a line to %s.\n", path);
    return 0;
}
