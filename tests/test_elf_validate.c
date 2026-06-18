/*
 * Host-side unit tests for the ELF loader's validation boundary
 * (main/elf_validate.c).
 *
 * These test the security-critical bounds checks that defend the loader
 * against corrupted/hostile program.elf files: header field validation,
 * symtab/strtab size caps, and relocation offset/symbol containment. The
 * functions under test are pure (no ESP-IDF, no I/O), so the suite compiles
 * with plain gcc.
 *
 * Build: see tests/run_tests.sh
 *   gcc tests/test_elf_validate.c main/elf_validate.c -Itests -Imain -o /tmp/test_elf_validate
 */
#include "greatest.h"
#include "elf_validate.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* elf_validate_header                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t magic;
    uint16_t machine;
    uint16_t shnum;
    uint16_t shentsize;   /* 0 -> use correct sizeof(elf32_shdr_t) */
    uint16_t shstrndx;
    bool expected;
    const char *desc;
} hdr_case;

static const hdr_case hdr_cases[] = {
    /* baseline valid (XTENSA) */
    { ELF_MAGIC, EM_XTENSA, 5, 0, 3, true,  "valid baseline" },

    /* magic / machine */
    { 0xDEADBEEF, EM_XTENSA, 5, 0, 3, false, "bad magic" },
    { ELF_MAGIC, EM_RISCV,  5, 0, 3, false, "wrong machine (RISCV vs XTENSA)" },

    /* e_shnum bounds (1..64) */
    { ELF_MAGIC, EM_XTENSA, 0, 0, 0, false, "e_shnum=0" },
    { ELF_MAGIC, EM_XTENSA, 1, 0, 0, true,  "e_shnum=1 (lower bound)" },
    { ELF_MAGIC, EM_XTENSA, 64, 0, 0, true, "e_shnum=64 (upper bound)" },
    { ELF_MAGIC, EM_XTENSA, 65, 0, 0, false, "e_shnum=65 (over cap)" },

    /* e_shentsize */
    { ELF_MAGIC, EM_XTENSA, 5, 1, 0, false, "e_shentsize too small" },
    { ELF_MAGIC, EM_XTENSA, 5, 200, 0, false, "e_shentsize too large" },

    /* e_shstrndx range */
    { ELF_MAGIC, EM_XTENSA, 5, 0, 5,  false, "e_shstrndx == e_shnum (OOB)" },
    { ELF_MAGIC, EM_XTENSA, 5, 0, 6,  false, "e_shstrndx > e_shnum (OOB)" },
    { ELF_MAGIC, EM_XTENSA, 5, 0, 4,  true,  "e_shstrndx = e_shnum-1 (valid)" },
    { ELF_MAGIC, EM_XTENSA, 5, 0, 0,  true,  "e_shstrndx=0 (special, always ok)" },
};

TEST header_validation(void) {
    char report[1024];
    report[0] = '\0';
    int fails = 0;
    size_t n = sizeof(hdr_cases) / sizeof(hdr_cases[0]);

    for (size_t i = 0; i < n; i++) {
        elf32_ehdr_t h;
        memset(&h, 0, sizeof(h));
        h.e_magic = hdr_cases[i].magic;
        h.e_machine = hdr_cases[i].machine;
        h.e_shnum = hdr_cases[i].shnum;
        h.e_shentsize = hdr_cases[i].shentsize ? hdr_cases[i].shentsize
                                                : (uint16_t)sizeof(elf32_shdr_t);
        h.e_shstrndx = hdr_cases[i].shstrndx;

        const char *err = NULL;
        bool got = elf_validate_header(&h, EM_XTENSA, &err);
        if (got != hdr_cases[i].expected) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] '%s': expected %d, got %d (err=%s)",
                     i, hdr_cases[i].desc, hdr_cases[i].expected, got,
                     err ? err : "(null)");
            fails++;
        }
    }
    if (fails) FAILm(report);
    PASS();
}

TEST header_null_rejected(void) {
    const char *err = NULL;
    ASSERT_FALSE(elf_validate_header(NULL, EM_XTENSA, &err));
    ASSERT(err != NULL);
    PASS();
}

/* err out-param is optional (NULL must not crash). */
TEST header_null_err_safe(void) {
    elf32_ehdr_t h;
    memset(&h, 0, sizeof(h));
    h.e_magic = ELF_MAGIC;
    h.e_machine = EM_XTENSA;
    h.e_shnum = 5;
    h.e_shentsize = sizeof(elf32_shdr_t);
    ASSERT(elf_validate_header(&h, EM_XTENSA, NULL));
    h.e_magic = 0;
    ASSERT_FALSE(elf_validate_header(&h, EM_XTENSA, NULL));
    PASS();
}

/* ------------------------------------------------------------------ */
/* elf_validate_symtab                                                */
/* ------------------------------------------------------------------ */

#define SYM_SIZE ((uint32_t)sizeof(elf32_sym_t))

typedef struct {
    uint32_t sym_size;
    uint32_t str_size;   /* used only if has_str */
    bool has_str;
    bool expected;
    const char *desc;
} sym_case;

static const sym_case sym_cases[] = {
    { 4 * SYM_SIZE, 256, true,  true,  "valid symtab + strtab" },
    { 4 * SYM_SIZE, 0,   false, true,  "valid symtab, no strtab" },

    /* symtab size bounds */
    { 0,             0, false, false, "symtab size = 0" },
    { 1024 * 1024,   0, false, true,  "symtab size = 1 MB (upper bound)" },
    { 1024 * 1024 + 1, 0, false, false, "symtab size > 1 MB (OOM cap)" },

    /* symtab multiple */
    { SYM_SIZE + 1, 0, false, false, "symtab size not a multiple of sym entry" },
    { SYM_SIZE,     0, false, true,  "symtab size = exactly one entry" },

    /* strtab bounds (only when present) */
    { 4 * SYM_SIZE, 0,           true, false, "strtab size = 0" },
    { 4 * SYM_SIZE, 1024 * 1024, true, true,  "strtab size = 1 MB (upper bound)" },
    { 4 * SYM_SIZE, 1024 * 1024 + 1, true, false, "strtab size > 1 MB" },
};

TEST symtab_validation(void) {
    char report[1024];
    report[0] = '\0';
    int fails = 0;
    size_t n = sizeof(sym_cases) / sizeof(sym_cases[0]);

    for (size_t i = 0; i < n; i++) {
        elf32_shdr_t sym_sh, str_sh;
        memset(&sym_sh, 0, sizeof(sym_sh));
        memset(&str_sh, 0, sizeof(str_sh));
        sym_sh.sh_size = sym_cases[i].sym_size;
        str_sh.sh_size = sym_cases[i].str_size;

        const char *err = NULL;
        bool got = elf_validate_symtab(&sym_sh,
                                       sym_cases[i].has_str ? &str_sh : NULL,
                                       &err);
        if (got != sym_cases[i].expected) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] '%s': expected %d, got %d (err=%s)",
                     i, sym_cases[i].desc, sym_cases[i].expected, got,
                     err ? err : "(null)");
            fails++;
        }
    }
    if (fails) FAILm(report);
    PASS();
}

TEST symtab_null_rejected(void) {
    const char *err = NULL;
    ASSERT_FALSE(elf_validate_symtab(NULL, NULL, &err));
    ASSERT(err != NULL);
    PASS();
}

/* ------------------------------------------------------------------ */
/* elf_reloc_offset_in_bounds                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t r_offset;
    uint32_t sh_addr;
    uint32_t sh_size;
    bool expected;
    const char *desc;
} reloc_off_case;

static const reloc_off_case off_cases[] = {
    /* section [0x1000, 0x1010), room for a uint32_t patch */
    { 0x1000, 0x1000, 0x10, true,  "offset at section start" },
    { 0x100C, 0x1000, 0x10, true,  "offset at last valid (start + size - 4)" },
    { 0x100D, 0x1000, 0x10, false, "offset leaves < 4 bytes (start+size-3)" },
    { 0x1010, 0x1000, 0x10, false, "offset at section end (no room)" },
    { 0x2000, 0x1000, 0x10, false, "offset well past section" },

    /* below section start: must short-circuit before unsigned subtraction */
    { 0x0FFF, 0x1000, 0x10, false, "offset below sh_addr (wrap guard)" },
    { 0x0000, 0x1000, 0x10, false, "offset 0, section higher" },

    /* section at addr 0 */
    { 0x0,    0x0,    0x10, true,  "section at 0, offset 0" },
    { 0x0C,   0x0,    0x10, true,  "section at 0, last valid offset" },
};

TEST reloc_offset_bounds(void) {
    char report[1024];
    report[0] = '\0';
    int fails = 0;
    size_t n = sizeof(off_cases) / sizeof(off_cases[0]);

    for (size_t i = 0; i < n; i++) {
        elf32_shdr_t sh;
        memset(&sh, 0, sizeof(sh));
        sh.sh_addr = off_cases[i].sh_addr;
        sh.sh_size = off_cases[i].sh_size;

        bool got = elf_reloc_offset_in_bounds(off_cases[i].r_offset, &sh);
        if (got != off_cases[i].expected) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] '%s': r_offset=0x%x sh_addr=0x%x sh_size=0x%x "
                     "expected %d got %d",
                     i, off_cases[i].desc, off_cases[i].r_offset,
                     off_cases[i].sh_addr, off_cases[i].sh_size,
                     off_cases[i].expected, got);
            fails++;
        }
    }
    if (fails) FAILm(report);
    PASS();
}

TEST reloc_offset_null_section_rejected(void) {
    ASSERT_FALSE(elf_reloc_offset_in_bounds(0, NULL));
    PASS();
}

/* ------------------------------------------------------------------ */
/* elf_reloc_sym_in_bounds                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t r_sym;
    int sym_count;
    bool expected;
    const char *desc;
} sym_idx_case;

static const sym_idx_case sidx_cases[] = {
    { 0,  10, true,  "first symbol" },
    { 9,  10, true,  "last valid (count-1)" },
    { 10, 10, false, "equal to count (OOB)" },
    { 11, 10, false, "past count" },
    { 0,  1,  true,  "single-symbol table, index 0" },
    { 1,  1,  false, "single-symbol table, index 1" },
    { 0,  0,  false, "empty table" },
    { 0, -1,  false, "negative count" },
};

TEST reloc_sym_bounds(void) {
    char report[512];
    report[0] = '\0';
    int fails = 0;
    size_t n = sizeof(sidx_cases) / sizeof(sidx_cases[0]);

    for (size_t i = 0; i < n; i++) {
        bool got = elf_reloc_sym_in_bounds(sidx_cases[i].r_sym,
                                           sidx_cases[i].sym_count);
        if (got != sidx_cases[i].expected) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] '%s': r_sym=%u count=%d expected %d got %d",
                     i, sidx_cases[i].desc, sidx_cases[i].r_sym,
                     sidx_cases[i].sym_count, sidx_cases[i].expected, got);
            fails++;
        }
    }
    if (fails) FAILm(report);
    PASS();
}

/* ------------------------------------------------------------------ */
/* suites                                                             */
/* ------------------------------------------------------------------ */

SUITE(header_suite) {
    RUN_TEST(header_validation);
    RUN_TEST(header_null_rejected);
    RUN_TEST(header_null_err_safe);
}

SUITE(symtab_suite) {
    RUN_TEST(symtab_validation);
    RUN_TEST(symtab_null_rejected);
}

SUITE(reloc_suite) {
    RUN_TEST(reloc_offset_bounds);
    RUN_TEST(reloc_offset_null_section_rejected);
    RUN_TEST(reloc_sym_bounds);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(header_suite);
    RUN_SUITE(symtab_suite);
    RUN_SUITE(reloc_suite);
    GREATEST_MAIN_END();
}
