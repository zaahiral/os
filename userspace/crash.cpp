#include "process/syscall.h"

// Writes to memory that the program does not own, which causes a page fault.
// The kernel stops the program, and the shell keeps running.
extern "C" void _start()
{
    syscall(SYSCALL_DPRINTF, (uint64_t)"Writing to address 0x10.\n");
    *(volatile uint64_t*)0x10 = 1;
    syscall(SYSCALL_DPRINTF, (uint64_t)"This line is never printed.\n");
    syscall(SYSCALL_EXIT, 0);
}
