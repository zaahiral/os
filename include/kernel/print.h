#ifndef PRINT_H
#define PRINT_H

#include "types.h"

// Uses the boot framebuffer when available, or the VGA text buffer otherwise.
void initialise_console();
bool has_framebuffer_console();
void clear_screen();
// Print counted bytes, including bytes after a null.
void print_bytes(const char* buffer, uint64_t count);
void printf(const char* str);
void printf(uint64_t num);

#endif
