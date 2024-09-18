#include "process/syscall.h"

// Prints a message using the system call interface.
extern "C" void _start()
{
    syscall(SYSCALL_DPRINTF, (uint64_t)"Hello from userspace!");
    while (1);
}
