#include "process/syscall.h"

extern "C" void _start()
{
    syscall(SYSCALL_DPRINTF, (uint64_t)"Executing cli in user space.\n");
    asm volatile("cli");
    syscall(SYSCALL_DPRINTF, (uint64_t)"The privileged instruction returned.\n");
    syscall(SYSCALL_EXIT, 0);
}
