#ifndef PRINT_H
#define PRINT_H

#include "types.h"

// Prints to the VGA text buffer.
void clear_screen();
void printf(const char* str);
void printf(uint64_t num);

#endif
