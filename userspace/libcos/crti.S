/* The start of the _init and _fini functions. crtbegin.o, crtend.o and crtn.o add to them, in that order. */

.section .init
.global _init
_init:
    push %rbp
    movq %rsp, %rbp

.section .fini
.global _fini
_fini:
    push %rbp
    movq %rsp, %rbp
