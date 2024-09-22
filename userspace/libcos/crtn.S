/* The end of the _init and _fini functions that crti.o starts. */

.section .init
    popq %rbp
    ret

.section .fini
    popq %rbp
    ret
