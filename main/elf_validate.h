#ifndef ELF_VALIDATE_H
#define ELF_VALIDATE_H

/*
 * ELF32 validation helpers — the security boundary of the dynamic loader.
 *
 * The on-disk ELF32 structure definitions and the bounds-checking logic that
 * guards the loader against corrupted/hostile program.elf files live here so
 * they can be unit-tested on the host without ESP-IDF (see
 * tests/test_elf_validate.c). The loader (main/elf_loader.c) includes this
 * header and delegates its reject-early checks to these functions.
 *
 * Every function is pure: it inspects only its arguments and touches no I/O,
 * no allocation, and no global state.
 */
#include <stdint.h>
#include <stdbool.h>

#define ELF_MAGIC 0x464C457F
#define EM_XTENSA 94
#define EM_RISCV  243

/* Cap on symtab/strtab sizes to defend against a malformed ELF claiming a
 * huge table and OOMing the device. 1 MB is far above what any real app
 * needs (typical is <100 KB) but bounded. */
#define ELF_TABLE_MAX_BYTES (1024 * 1024)

typedef struct __attribute__((packed)) {
    uint32_t e_magic;
    uint8_t  e_class;
    uint8_t  e_data;
    uint8_t  e_ident_ver;
    uint8_t  e_ident_osabi;
    uint8_t  e_ident_abiver;
    uint8_t  e_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

typedef struct __attribute__((packed)) {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} elf32_shdr_t;

typedef struct __attribute__((packed)) {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} elf32_sym_t;

/*
 * Validate ELF header fields against corrupted/hostile files.
 * expected_machine is EM_XTENSA or EM_RISCV. On failure, *err (if non-NULL)
 * is set to a static reason string. Returns true iff the header is safe to
 * proceed from (magic, machine, e_shnum in 1..64, e_shentsize matches, and
 * e_shstrndx in range).
 */
bool elf_validate_header(const elf32_ehdr_t *ehdr, uint16_t expected_machine,
                         const char **err);

/*
 * Validate symtab/strtab section-header sizes: non-zero, <= the 1 MB OOM cap,
 * and symtab size a multiple of the symbol entry size. str_sh may be NULL
 * (no string table). On failure *err is set to a static reason.
 */
bool elf_validate_symtab(const elf32_shdr_t *sym_sh,
                         const elf32_shdr_t *str_sh, const char **err);

/*
 * True if r_offset falls within target_sh's [sh_addr, sh_addr + sh_size)
 * range with room for a uint32_t patch. Guards the relocation write against
 * out-of-bounds access. Preserves the loader's original (pre-extraction)
 * semantics, including the r_offset < sh_addr short-circuit that prevents an
 * unsigned wrap on the subtraction.
 */
bool elf_reloc_offset_in_bounds(uint32_t r_offset,
                                const elf32_shdr_t *target_sh);

/*
 * True if relocation symbol index r_sym is within [0, sym_count). Guards the
 * symtab[r_sym] lookup against an out-of-bounds read.
 */
bool elf_reloc_sym_in_bounds(uint32_t r_sym, int sym_count);

#endif /* ELF_VALIDATE_H */
