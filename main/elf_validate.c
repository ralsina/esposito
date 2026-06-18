/*
 * Pure ELF validation logic — no ESP-IDF, no I/O, no allocation.
 * See elf_validate.h for the contract. Tested by tests/test_elf_validate.c.
 */
#include "elf_validate.h"
#include <stddef.h>

bool elf_validate_header(const elf32_ehdr_t *ehdr, uint16_t expected_machine,
                         const char **err) {
    if (err) *err = NULL;
    if (!ehdr) { if (err) *err = "null header"; return false; }

    if (ehdr->e_magic != ELF_MAGIC) {
        if (err) *err = "bad magic";
        return false;
    }
    if (ehdr->e_machine != expected_machine) {
        if (err) *err = "wrong machine";
        return false;
    }
    /* e_shnum: 0 means use the section at index 0's sh_size (rare); > 64
     * risks OOM / OOB indexing into shdrs[]. Cap at 1..64. */
    if (ehdr->e_shnum == 0 || ehdr->e_shnum > 64) {
        if (err) *err = "out-of-range e_shnum";
        return false;
    }
    if (ehdr->e_shentsize != sizeof(elf32_shdr_t)) {
        if (err) *err = "bad e_shentsize";
        return false;
    }
    if (ehdr->e_shstrndx != 0 && ehdr->e_shstrndx >= ehdr->e_shnum) {
        if (err) *err = "out-of-range e_shstrndx";
        return false;
    }
    return true;
}

bool elf_validate_symtab(const elf32_shdr_t *sym_sh,
                         const elf32_shdr_t *str_sh, const char **err) {
    if (err) *err = NULL;
    if (!sym_sh) { if (err) *err = "null symtab section"; return false; }

    if (sym_sh->sh_size == 0 || sym_sh->sh_size > ELF_TABLE_MAX_BYTES) {
        if (err) *err = "out-of-range symtab size";
        return false;
    }
    if (sym_sh->sh_size % sizeof(elf32_sym_t) != 0) {
        if (err) *err = "symtab size not a multiple of sym entry size";
        return false;
    }
    if (str_sh && (str_sh->sh_size == 0 || str_sh->sh_size > ELF_TABLE_MAX_BYTES)) {
        if (err) *err = "out-of-range strtab size";
        return false;
    }
    return true;
}

bool elf_reloc_offset_in_bounds(uint32_t r_offset,
                                const elf32_shdr_t *target_sh) {
    if (!target_sh) return false;
    /* Short-circuit: if r_offset < sh_addr the unsigned subtraction would
     * wrap, so check first (matches the original inline logic exactly). */
    if (r_offset < target_sh->sh_addr) return false;
    if ((r_offset - target_sh->sh_addr + sizeof(uint32_t)) > target_sh->sh_size) {
        return false;
    }
    return true;
}

bool elf_reloc_sym_in_bounds(uint32_t r_sym, int sym_count) {
    if (sym_count <= 0) return false;
    return r_sym < (uint32_t)sym_count;
}
