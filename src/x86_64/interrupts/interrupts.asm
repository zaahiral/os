section .text
global isr_table
extern _ZN3CPU16InterruptManager20handle_err_interruptEhhy
extern _ZN3CPU16InterruptManager22handle_noerr_interruptEhy
extern isr_def_32

%macro pushaq 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro popaq 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

%macro error_isr 1
isr_def_%+%1:
    pop rsi
    pushaq
    mov rdi, %1
    ; Pass the interrupted code segment, which is above the 15 saved registers and the return address.
    mov rdx, [rsp + 16*8]
    call _ZN3CPU16InterruptManager20handle_err_interruptEhhy
    popaq
    iretq
%endmacro

%macro no_error_isr 1
isr_def_%+%1:
    pushaq
    mov rdi, %1
    ; Pass the interrupted code segment, which is above the 15 saved registers and the return address.
    mov rsi, [rsp + 16*8]
    call _ZN3CPU16InterruptManager22handle_noerr_interruptEhy
    popaq
    iretq
%endmacro

; Unfortunately, there is not really a better way to use the macros
no_error_isr 0
no_error_isr 1
no_error_isr 2
no_error_isr 3
no_error_isr 4
no_error_isr 5
no_error_isr 6
no_error_isr 7
error_isr    8
no_error_isr 9
error_isr    10
error_isr    11
error_isr    12
error_isr    13
error_isr    14
no_error_isr 15
no_error_isr 16
error_isr    17
no_error_isr 18
no_error_isr 19
no_error_isr 20
no_error_isr 21
no_error_isr 22
no_error_isr 23
no_error_isr 24
no_error_isr 25
no_error_isr 26
no_error_isr 27
no_error_isr 28
no_error_isr 29
error_isr    30
no_error_isr 31
; The PIT ISR (32) is written in asm for task switching
no_error_isr 33
no_error_isr 34
no_error_isr 35
no_error_isr 36
no_error_isr 37
no_error_isr 38
no_error_isr 39
no_error_isr 40
no_error_isr 41
no_error_isr 42
no_error_isr 43
no_error_isr 44
no_error_isr 45
no_error_isr 46
no_error_isr 47
no_error_isr 48

isr_table:
%assign i 0 
%rep    49
    dq isr_def_%+i
%assign i i+1 
%endrep
