#ifndef OUTPUT_H
#define OUTPUT_H

#include "process/syscall.h"

// Prints a string with the system call interface.
void print(const char* text)
{
    uint64_t length = 0;
    while (text[length]) { length++; }
    syscall(SYSCALL_FWRITE, 1, (uint64_t)text, length);
}

// Prints a number in base 10, or base 16 with a 0x prefix.
void print_number(uint64_t number, uint64_t base = 10)
{
    char digits[21];
    char buffer[24];
    uint64_t count = 0;
    do
    {
        digits[count++] = "0123456789ABCDEF"[number % base];
        number /= base;
    }
    while (number);
    uint64_t length = 0;
    if (base == 16)
    {
        buffer[length++] = '0';
        buffer[length++] = 'x';
    }
    while (count) { buffer[length++] = digits[--count]; }
    buffer[length] = '\0';
    print(buffer);
}

#endif
