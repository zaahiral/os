#ifndef GUARD_ABI_H
#define GUARD_ABI_H

#include "types.h"

extern "C" int __cxa_guard_acquire (uint64_t *);
extern "C" void __cxa_guard_release (uint64_t *);
extern "C" int __cxa_atexit(void (*)(void*), void*, void*);

#endif
