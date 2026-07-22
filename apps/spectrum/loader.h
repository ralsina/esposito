#ifndef LOADER_H
#define LOADER_H

#include <stddef.h>

#define LOADER_UNKNOWN 0
#define LOADER_Z80     1
#define LOADER_SNA     2
#define LOADER_TAP     3

int loader_load_z80(const char *path);
int loader_load_z80_from_buffer(const unsigned char *buf, size_t size);
int loader_get_type(const char *path);

#endif
