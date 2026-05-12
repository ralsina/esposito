#ifndef OS_SYMTAB_H
#define OS_SYMTAB_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *name;
    void *addr;
} os_symtab_entry_t;

const os_symtab_entry_t *os_symtab_lookup(const char *name);
int os_symtab_count(void);
const os_symtab_entry_t *os_symtab_get(int index);

#endif
