#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include "process/syscall.h"
#include "string.h"

// Output is collected in a buffer and printed with one system call when the buffer fills or the call ends.
#define PRINTF_BUFFER_SIZE 256

struct PrintBuffer
{
    char text[PRINTF_BUFFER_SIZE];
    uint64_t length = 0;

    void flush()
    {
        text[length] = '\0';
        syscall(SYSCALL_FWRITE, 1, (uint64_t)text, length);
        length = 0;
    }

    void put(char c)
    {
        if (length == PRINTF_BUFFER_SIZE - 1) { flush(); }
        text[length++] = c;
    }
};

// Write a number in a base, padded to a width with spaces or zeros.
void print_unsigned(PrintBuffer& buffer, uint64_t number, uint64_t base, uint64_t width, char padding)
{
    char digits[21];
    uint64_t count = 0;
    do
    {
        digits[count++] = "0123456789abcdef"[number % base];
        number /= base;
    }
    while (number);
    for (; width > count; width--) { buffer.put(padding); }
    while (count) { buffer.put(digits[--count]); }
}

// Supports %d, %u, %x, %s, %c and %%, with an l before d, u or x for 64-bit numbers, and a width such as %5d or %08x.
int vprintf(const char* format, va_list arguments)
{
    PrintBuffer buffer;
    for (; *format; format++)
    {
        if (*format != '%')
        {
            buffer.put(*format);
            continue;
        }
        format++;
        char padding = ' ';
        if (*format == '0') { padding = '0'; format++; }
        uint64_t width = 0;
        while (*format >= '0' && *format <= '9') { width = width * 10 + (*format++ - '0'); }
        bool isLong = *format == 'l';
        if (isLong) { format++; }

        switch (*format)
        {
        case 'd':
        {
            int64_t number = isLong ? va_arg(arguments, int64_t) : va_arg(arguments, int);
            if (number < 0)
            {
                buffer.put('-');
                number = -number;
                if (width) { width--; }
            }
            print_unsigned(buffer, number, 10, width, padding);
            break;
        }
        case 'u':
            print_unsigned(buffer, isLong ? va_arg(arguments, uint64_t) : va_arg(arguments, unsigned int), 10, width, padding);
            break;
        case 'x':
            print_unsigned(buffer, isLong ? va_arg(arguments, uint64_t) : va_arg(arguments, unsigned int), 16, width, padding);
            break;
        case 's':
        {
            const char* text = va_arg(arguments, const char*);
            for (uint64_t length = strlen(text); width > length; width--) { buffer.put(' '); }
            while (*text) { buffer.put(*text++); }
            break;
        }
        case 'c':
            buffer.put((char)va_arg(arguments, int));
            break;
        case '%':
            buffer.put('%');
            break;
        default:
            return -1;
        }
    }
    buffer.flush();
    return 0;
}

int printf(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

int puts(const char* text)
{
    syscall(SYSCALL_FWRITE, 1, (uint64_t)text, strlen(text));
    syscall(SYSCALL_FWRITE, 1, (uint64_t)"\n", 1);
    return 0;
}

#endif
