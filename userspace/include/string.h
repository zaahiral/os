#ifndef STRING_H
#define STRING_H

#include "types.h"

uint64_t strlen(const char* text)
{
    uint64_t length = 0;
    while (text[length]) { length++; }
    return length;
}

int strcmp(const char* a, const char* b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *(const uint8_t*)a - *(const uint8_t*)b;
}

int strncmp(const char* a, const char* b, uint64_t count)
{
    while (count && *a && *a == *b)
    {
        a++;
        b++;
        count--;
    }
    if (!count) { return 0; }
    return *(const uint8_t*)a - *(const uint8_t*)b;
}

char* strcpy(char* destination, const char* source)
{
    char* start = destination;
    while ((*destination++ = *source++));
    return start;
}

// Return the first occurrence of a character in a string, or nullptr if it does not occur.
const char* strchr(const char* text, char c)
{
    for (; *text; text++)
    {
        if (*text == c) { return text; }
    }
    return c ? nullptr : text;
}

// The compiler may call memcpy and memset itself, so they have C linkage and the standard signatures.
extern "C" void* memcpy(void* destination, const void* source, uint64_t count)
{
    auto d = (uint8_t*)destination;
    auto s = (const uint8_t*)source;
    while (count--) { *d++ = *s++; }
    return destination;
}

extern "C" void* memset(void* destination, int value, uint64_t count)
{
    auto d = (uint8_t*)destination;
    while (count--) { *d++ = value; }
    return destination;
}

int memcmp(const void* a, const void* b, uint64_t count)
{
    auto x = (const uint8_t*)a;
    auto y = (const uint8_t*)b;
    for (; count; count--, x++, y++)
    {
        if (*x != *y) { return *x - *y; }
    }
    return 0;
}

#endif
