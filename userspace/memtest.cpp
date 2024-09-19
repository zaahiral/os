#include "process/syscall.h"
#include "output.h"
#include "malloc.h"

constexpr uint64_t BLOCK_COUNT = 100;

// Allocates and frees memory with malloc, which uses brk, and maps memory directly with mmap.
extern "C" void _start()
{
    print("The heap starts at ");
    print_number(syscall(SYSCALL_BRK, 0), 16);
    print(".\n");

    // Allocate blocks of different sizes, and fill each block with its own index.
    uint8_t* blocks[BLOCK_COUNT];
    for (uint64_t i = 0; i < BLOCK_COUNT; i++)
    {
        blocks[i] = (uint8_t*)malloc(i * 10 + 1);
        for (uint64_t j = 0; j < i * 10 + 1; j++) { blocks[i][j] = i; }
    }

    // Free every second block, and allocate again so that the freed blocks are reused.
    for (uint64_t i = 0; i < BLOCK_COUNT; i += 2) { free(blocks[i]); }
    uint64_t reusedCount = 0;
    for (uint64_t i = 0; i < BLOCK_COUNT; i += 2)
    {
        auto block = (uint8_t*)malloc(i * 10 + 1);
        if (block == blocks[i]) { reusedCount++; }
        for (uint64_t j = 0; j < i * 10 + 1; j++) { block[j] = i; }
        blocks[i] = block;
    }

    // Check that no block was overwritten by another.
    uint64_t corruptCount = 0;
    for (uint64_t i = 0; i < BLOCK_COUNT; i++)
    {
        for (uint64_t j = 0; j < i * 10 + 1; j++)
        {
            if (blocks[i][j] != i) { corruptCount++; break; }
        }
    }
    print("Allocated ");
    print_number(BLOCK_COUNT);
    print(" blocks, reused ");
    print_number(reusedCount);
    print(" freed blocks, and found ");
    print_number(corruptCount);
    print(" corrupt blocks.\n");
    print("The heap now ends at ");
    print_number(syscall(SYSCALL_BRK, 0), 16);
    print(".\n");

    // Map 64KiB with mmap, check that it starts as zeros, fill it, and unmap it.
    uint64_t length = 64 * 1024;
    auto mapped = (uint64_t*)syscall(SYSCALL_VMMAP, length);
    uint64_t nonZeroCount = 0;
    for (uint64_t i = 0; i < length / sizeof(uint64_t); i++)
    {
        if (mapped[i]) { nonZeroCount++; }
        mapped[i] = i;
    }
    print("Mapped 64KiB at ");
    print_number((uint64_t)mapped, 16);
    print(" with ");
    print_number(nonZeroCount);
    print(" non-zero words, and unmapped it.\n");
    syscall(SYSCALL_VMUNMAP, (uint64_t)mapped, length);
    syscall(SYSCALL_EXIT, corruptCount);
}
