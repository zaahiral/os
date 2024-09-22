#include <stdlib.h>

/* The start of every program built with the cOS toolchain. */

extern int main(int argc, char** argv, char** envp);
/* newlib defines environ, which starts empty, as cOS has no environment variables yet. */
extern char** environ;
extern void __libc_init_array(void);
extern void __libc_fini_array(void);

/* The kernel starts programs here, with argc and argv in rdi and rsi, as if _start had been called. */
void _start(int argc, char** argv)
{
    /* Run global constructors, and arrange for global destructors to run when the program exits. */
    atexit(__libc_fini_array);
    __libc_init_array();
    exit(main(argc, argv, environ));
}
