#ifndef STRING_H
#define STRING_H

#include "types.h"

static char* strcpy(char* dst, char* str)
{
    char* dptr = dst;
    while (*dptr++=*str++);
    return dst;
}

static char* strncpy(char* dst, char* str, uint64_t num)
{
    char* dptr = dst;
    while ((*dptr++=*str++)&&(--num));
    for (; num-- ; *dst++ = '\0');
    return dst;
}

static uint64_t strlen(const char* str)
{
    uint64_t length = 0;
    while (str[length]) { length++; }
    return length;
}

static int strcmp(const char* s1, const char* s2)
{
    while(*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}

#endif
