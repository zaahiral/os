section .multiboot_header
extern uefi_start
KERNEL_V_BASE EQU 0xFFFFFF8000000000
header_start:
    dd 0xe85250d6 ; magic number to identify the header 
    dd 0 ; start in 32-bit protected mode
    dd header_end - header_start ; length of header
    dd 0x100000000 - (0xe85250d6 + (header_end - header_start)) ; checksum

    ; The optional EFI entry leaves the GRUB protected-mode entry unchanged.
    dw 9
    dw 1
    dd 12
    dd uefi_start - KERNEL_V_BASE
    dd 0

    ; end tag
    dw 0
    dw 0
    dd 8
header_end:
