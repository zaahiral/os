global long_mode_start
global uefi_start
extern call_constructors
extern pre_kernel
extern stackTop
extern gdt64.pointer
extern gdt64
extern tss64
section .text
bits 64

; UEFI calls this physical address with the Multiboot2 information address in EBX.
; Firmware still provides identity mappings, and boot services have already ended.
uefi_start:
    cli
    cld
    lea rsp, [rel stackTop]
    lea rdi, [rel uefiPageTables]
    lea rax, [rel uefiPageTables + 4096]
    or rax, 3
    mov [rdi], rax
    mov [rdi + 511 * 8], rax
    lea rdi, [rel uefiPageTables + 4096]
    mov eax, 0x83
    mov ecx, 512
.pages:
    stosq
    add rax, 0x40000000
    loop .pages
    lea rax, [rel uefiPageTables]
    mov cr3, rax
    lgdt [rel gdt64.pointer]
    push 8
    mov rax, higher_half_switch
    push rax
    retfq

long_mode_start:
    cli
    mov rax, higher_half_switch
    jmp rax
higher_half_switch:
    cld
    mov rsp, stackTop
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call call_constructors
    mov edi, ebx
    ; We pass structure pointers onto the pre_kernel
    mov rsi, gdt64.pointer 
    lgdt [rsi]
    mov rdx, gdt64
    mov rcx, tss64
    call pre_kernel 
    hlt

section .bss
align 4096
uefiPageTables:
    resb 4096 * 2
