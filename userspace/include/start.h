#ifndef START_H
#define START_H

#include "process/syscall.h"

// Programs that include this header start at main, and exit with the value that it returns.
int main(int argc, char** argv);

extern "C" void _start(int argc, char** argv)
{
    syscall(SYSCALL_EXIT, (uint64_t)main(argc, argv));
}

#endif
