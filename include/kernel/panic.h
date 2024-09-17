#ifndef PANIC_H
#define PANIC_H

namespace Kernel
{
    // Prints the reason and halts the processor. This function does not return.
    [[noreturn]] void panic(const char* reason);
}

#endif
