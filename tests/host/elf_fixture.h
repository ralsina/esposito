#ifndef ELF_FIXTURE_H
#define ELF_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * ELF fixture builder — constructs minimal valid (and deliberately invalid)
 * ELF32 binaries in memory and writes them to temp files, so the host-side
 * harness can drive elf_loader_load() end-to-end.
 *
 * The base fixture is a minimal PIC app for XTENSA:
 *   - .text      (SHF_ALLOC|SHF_EXECINSTR) — 16 bytes of dummy code
 *   - .rela.text (SHT_RELA, sh_info -> .text) — one R_XTENSA_32 relocation
 *                 against an external symbol "host_export_a"
 *   - .symtab    (SHT_SYMTAB) — null sym, app_init (local), host_export_a (undef)
 *   - .strtab    (SHT_STRTAB) — symbol name strings
 *   - .shstrtab  (SHT_STRTAB) — section name strings
 *
 * Variants corrupt specific fields to exercise the loader's rejection paths.
 */

typedef struct {
    bool valid_magic;       /* false -> corrupt e_magic */
    uint16_t machine;       /* EM_XTENSA for a valid build */
    bool include_symtab;    /* false -> omit .symtab (loader must reject) */
    bool include_app_init;  /* false -> no app_init symbol */
    bool include_reloc;     /* include the .rela.text relocation section */
    bool reloc_oob;         /* r_offset past .text end (must be skipped, not crash) */
} elf_fixture_opts_t;

/* Sensible defaults for a valid fixture. */
#define ELF_FIXTURE_VALID ((elf_fixture_opts_t){ \
    .valid_magic = true, .machine = 94 /*EM_XTENSA*/, \
    .include_symtab = true, .include_app_init = true, \
    .include_reloc = true, .reloc_oob = false })

/*
 * Build an ELF per opts and write it to a unique temp file.
 * Returns a malloc'd path string (caller must free() and unlink the file).
 * Returns NULL on failure.
 */
char *elf_fixture_build(const elf_fixture_opts_t *opts);

/* Convenience wrappers. */
char *elf_fixture_valid(void);
char *elf_fixture_bad_magic(void);
char *elf_fixture_wrong_machine(void);
char *elf_fixture_no_symtab(void);
char *elf_fixture_no_app_init(void);

#endif /* ELF_FIXTURE_H */
