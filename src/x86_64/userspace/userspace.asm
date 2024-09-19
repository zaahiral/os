global asm_flush_tss
global enable_syscall_sysret
global start_userspace_task_sysret
global start_userspace_task_iretq
global syscall_kernel_stack
extern _ZN9Processes15syscall_handlerEyyyyyy
bits 64

section .data
; The top of the current task's kernel stack, which is updated on every task switch.
syscall_kernel_stack: dq 0
; The user stack pointer is kept here only until it is pushed onto the kernel stack.
syscall_user_stack: dq 0

section .text

; Load TSS selector
asm_flush_tss: 
    mov ax, 0x30
    ltr ax
    ret

asm_syscall_handler:
    ; syscall does not switch stacks, so we move to the task's kernel stack. Interrupts are masked on entry until we have.
    mov [rel syscall_user_stack], rsp
    mov rsp, [rel syscall_kernel_stack]
    push qword [rel syscall_user_stack]
    sti
    push rcx
    push r11
    ; We preserve rcx for sysret, so we can clobber it for arguments.
    mov rcx, r10
    call _ZN9Processes15syscall_handlerEyyyyyy
    pop r11
    pop rcx
    ; Switch back to the user stack with interrupts disabled, and sysret restores the user's flags.
    cli
    pop rsp
    o64 sysret

enable_syscall_sysret:
    mov rcx, 0xc0000080
    rdmsr
    ; Enable syscall/sysret.
    or eax, 1 
    wrmsr
    mov rcx, 0xc0000081 
    rdmsr
    ; Load code/stack segments for PL0/3 into IA32_STAR.
    mov edx, 0x00180008 
    wrmsr
    mov rcx, 0xc0000082
    mov rdi, asm_syscall_handler
    ; Load syscall handler into IA32_LSTAR.
    mov eax, edi 
    shr rdi, 32
    mov edx, edi
    wrmsr
    mov rcx, 0xc0000084
    ; Mask the interrupt flag in IA32_FMASK, so that interrupts are disabled when syscall is executed.
    mov eax, 0x200
    xor edx, edx
    wrmsr
    ret

; Fake a syscall stack to sysret into ring 3.
start_userspace_task_sysret: 
    mov rcx, rdi
    ; Enable interrupts in RFLAGS.
    mov r11, 0x202
    mov rsp, rsi
    o64 sysret

; Fake an interrupt stack to iret into ring 3.
start_userspace_task_iretq: 
    ; User-space data selector.
    push (4*8)|3
    ; Target stack pointer.
    push rsi 
    ; Enable interrupts in RFLAGS.
    push 0x202
    ; User-space code selector.
    push (5*8)|3
    ; Target instruction pointer.
    push rdi
    iretq

start_kernel_task:
    mov rsp, rsi
    call rdi
