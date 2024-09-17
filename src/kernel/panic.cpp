#include "panic.h"
#include "print.h"

namespace Kernel
{
    void panic(const char* reason)
    {
        asm volatile("cli");
        printf("\nKERNEL PANIC: ");
        printf(reason);
        while (1)
            asm volatile("hlt");
    }
}
