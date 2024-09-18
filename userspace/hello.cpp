#include "process/syscall.h"

// Prints a message using the system call interface.
extern "C" void _start()
{
    syscall(SYSCALL_DPRINTF, (uint64_t)"Hello from userspace!\n");
    syscall(SYSCALL_EXIT, 0);
}
