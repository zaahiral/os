#include "process/syscall.h"

// Writes a number into a buffer as decimal digits.
void format_number(uint64_t number, char* buffer)
{
    char digits[21];
    uint64_t count = 0;
    do
    {
        digits[count++] = '0' + number % 10;
        number /= 10;
    }
    while (number);
    for (uint64_t i = 0; i < count; i++)
        buffer[i] = digits[count - 1 - i];
    buffer[count] = '\0';
}

// Starts another program, waits for it to exit, and prints its exit code.
extern "C" void _start()
{
    syscall(SYSCALL_DPRINTF, (uint64_t)"Starting initrd/bin/readfile.\n");
    uint64_t tid = syscall(SYSCALL_SPAWN, (uint64_t)"initrd/bin/readfile");
    uint64_t exitCode = syscall(SYSCALL_WAIT, tid);

    char number[21];
    format_number(exitCode, number);
    syscall(SYSCALL_DPRINTF, (uint64_t)"readfile exited with code ");
    syscall(SYSCALL_DPRINTF, (uint64_t)number);
    syscall(SYSCALL_DPRINTF, (uint64_t)".\n");
    syscall(SYSCALL_EXIT, 0);
}
