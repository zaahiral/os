#ifndef STDLIB_H
#define STDLIB_H

#include "process/syscall.h"
#include "malloc.h"
#include "string.h"

[[noreturn]] void exit(int code)
{
    syscall(SYSCALL_EXIT, (uint64_t)code);
    while (1);
}

void* calloc(uint64_t count, uint64_t size)
{
    void* pointer = malloc(count * size);
    if (pointer) { memset(pointer, 0, count * size); }
    return pointer;
}

// Move an allocation to a block of a new size, copying as much of its contents as fit.
void* realloc(void* pointer, uint64_t size)
{
    if (!pointer) { return malloc(size); }
    uint64_t oldSize = ((BlockHeader*)pointer - 1)->size;
    if (oldSize >= size) { return pointer; }
    void* newPointer = malloc(size);
    if (!newPointer) { return nullptr; }
    memcpy(newPointer, pointer, oldSize);
    free(pointer);
    return newPointer;
}

int atoi(const char* text)
{
    int value = 0;
    bool isNegative = *text == '-';
    if (isNegative) { text++; }
    while (*text >= '0' && *text <= '9') { value = value * 10 + (*text++ - '0'); }
    return isNegative ? -value : value;
}

#endif
