ENTRY(_start)

KERNEL_V_BASE = 0xFFFFFF8000000000;

SECTIONS
{
    /* The kernel is loaded at 1MiB in physical memory and mapped at KERNEL_V_BASE + 1MiB. */
    . = KERNEL_V_BASE + 1M;
    start_of_kernel_image = .;

    .boot : AT(ADDR(.boot) - KERNEL_V_BASE)
    {
        /* The multiboot header must be within the first 32KiB of the image. */
        KEEP(*(.multiboot_header))
    }

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_V_BASE)
    {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_V_BASE)
    {
        /* Global constructors, called by call_constructors before pre_kernel. */
        start_ctors = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(.ctors))
        end_ctors = .;
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_V_BASE)
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_V_BASE)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(4K);
    end_of_kernel_image = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
