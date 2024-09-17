//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

#ifndef MURMURHASH3_H
#define MURMURHASH3_H

#include "types.h"

void MurmurHash3_x64_128(const void* key, int len, uint32_t seed, void* out);

#endif
