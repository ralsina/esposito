#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct elf_handle elf_handle_t;

elf_handle_t *elf_loader_load(const char *path);
void elf_loader_unload(elf_handle_t *handle);
void *elf_loader_symbol(elf_handle_t *handle, const char *name);

#endif
