#ifndef MALLOC_H
#define MALLOC_H

#include "process/syscall.h"

// A first-fit memory allocator for user-space programs, which grows the heap with the brk system call.
// Each block starts with a header, and the blocks form a list in the order they were made.
// Freed blocks are reused, but they are not split or joined with their neighbours yet.

struct BlockHeader
{
    uint64_t size;
    bool isFree;
    BlockHeader* next;
};

static BlockHeader* firstBlock = nullptr;
static BlockHeader* lastBlock = nullptr;

void* malloc(uint64_t size)
{
    // Sizes are rounded up to 16 bytes, so that every allocation is 16-byte aligned.
    size = (size + 15) & ~(uint64_t)15;
    for (auto block = firstBlock; block; block = block->next)
    {
        if (block->isFree && block->size >= size)
        {
            block->isFree = false;
            return block + 1;
        }
    }

    // No free block is large enough, so we move the program break to make a new one.
    uint64_t blockAddress = syscall(SYSCALL_BRK, 0);
    uint64_t newBreak = blockAddress + sizeof(BlockHeader) + size;
    if (syscall(SYSCALL_BRK, newBreak) != newBreak) { return nullptr; }

    auto block = (BlockHeader*)blockAddress;
    block->size = size;
    block->isFree = false;
    block->next = nullptr;
    if (lastBlock) { lastBlock->next = block; }
    else { firstBlock = block; }
    lastBlock = block;
    return block + 1;
}

void free(void* pointer)
{
    if (!pointer) { return; }
    ((BlockHeader*)pointer - 1)->isFree = true;
}

#endif
