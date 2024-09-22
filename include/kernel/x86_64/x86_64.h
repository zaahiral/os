#ifndef X86_64_H
#define X86_64_H

#include "types.h"

namespace CPU
{
    const uint64_t INTERRUPT_FLAG = 0x0200;

    static inline uint64_t read_rflags()
    {
        uint64_t rflags;
        asm volatile ("pushfq; popq %0" : "=r" (rflags));
        return rflags;
    }

    static inline void cli()
    {
        asm volatile ("cli");
    }

    static inline void sti()
    {
        asm volatile ("sti");
    }

    static inline void hlt()
    {
        asm volatile ("hlt");
    }

    // Allow SSE instructions, which compiled user-space programs use. The kernel itself is compiled without SSE.
    static inline void enable_sse()
    {
        uint64_t cr0;
        asm volatile ("mov %%cr0, %0" : "=r" (cr0));
        // Clear the emulation bit and set the monitor coprocessor bit.
        cr0 = (cr0 & ~(1ull << 2)) | (1ull << 1);
        asm volatile ("mov %0, %%cr0" : : "r" (cr0));
        uint64_t cr4;
        asm volatile ("mov %%cr4, %0" : "=r" (cr4));
        // Tell the processor that we save SSE state with fxsave, and handle SSE exceptions.
        cr4 |= (1ull << 9) | (1ull << 10);
        asm volatile ("mov %0, %%cr4" : : "r" (cr4));
        asm volatile ("fninit");
    }

    // The FPU and SSE state is 512 bytes, and the buffer must be 16-byte aligned.
    static constexpr uint64_t FPU_STATE_SIZE = 512;

    static inline void save_fpu_state(void* state)
    {
        asm volatile ("fxsave (%0)" : : "r" (state) : "memory");
    }

    static inline void restore_fpu_state(void* state)
    {
        asm volatile ("fxrstor (%0)" : : "r" (state) : "memory");
    }
}

#endif
