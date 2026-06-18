/*
 * ELF fixture builder implementation.
 *
 * Constructs a minimal ELF32 binary with the following layout:
 *
 *   [ELF header]           52 bytes
 *   [.text data]           16 bytes  (dummy XTENSA code)
 *   [.rela.text data]      12 bytes  (one RELA entry)
 *   [.symtab data]         48 bytes  (3 x elf32_sym_t: null, app_init, host_export_a)
 *   [.strtab data]         23 bytes  ("\0app_init\0host_export_a\0")
 *   [.shstrtab data]       38 bytes  ("\0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0")
 *   [padding to 4-byte align]
 *   [section headers]      6 x 40 bytes
 *
 * Section indices:
 *   0: SHN_UNDEF (null, required)
 *   1: .text      PROGBITS, ALLOC|EXECINSTR
 *   2: .rela.text RELA,      sh_info=1 (targets .text), sh_link=3 (symtab)
 *   3: .symtab    SYMTAB,    sh_link=4 (strtab), sh_info=1 (first non-local sym)
 *   4: .strtab    STRTAB
 *   5: .shstrtab  STRTAB     (referenced by e_shstrndx)
 */
#include "elf_fixture.h"
#include "elf_validate.h"   /* struct defs + ELF_MAGIC/EM_* constants */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ELF32 RELA entry (loader-local, not in elf_validate.h). */
typedef struct __attribute__((packed)) {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} elf32_rela_t;

/* ELF constants not in elf_validate.h (loader-local). */
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4

#define SHF_ALLOC     2
#define SHF_EXECINSTR 4

#define R_XTENSA_32  1

/* Layout helpers — each piece follows the previous. */
typedef struct {
    uint32_t text_off, text_size;
    uint32_t rela_off, rela_size;
    uint32_t sym_off,  sym_size;
    uint32_t str_off,  str_size;
    uint32_t shstr_off, shstr_size;
    uint32_t shoff;       /* section header table offset */
    uint16_t shnum;       /* number of section headers */
} layout_t;

/* Build the string table contents. */
static void build_strtab(char *buf, bool include_app_init) {
    /* Offset 0: null terminator (empty string for symbol index 0) */
    int off = 0;
    buf[off++] = '\0';
    /* app_init at offset 1 */
    if (include_app_init) {
        strcpy(buf + off, "app_init");
        off += 9;  /* "app_init\0" */
    }
    /* host_export_a */
    strcpy(buf + off, "host_export_a");
    off += 15;  /* "host_export_a\0" */
}

static void build_shstrtab(char *buf, bool include_reloc, bool include_symtab) {
    int off = 0;
    buf[off++] = '\0';                   /* 0: "" */
    strcpy(buf + off, ".text");     off += 6;  /* 1 */
    if (include_reloc) {
        strcpy(buf + off, ".rela.text"); off += 11;
    }
    if (include_symtab) {
        strcpy(buf + off, ".symtab");    off += 8;
        strcpy(buf + off, ".strtab");    off += 8;
    }
    strcpy(buf + off, ".shstrtab"); off += 9;
}

static void compute_layout(layout_t *l, const elf_fixture_opts_t *opts) {
    uint32_t off = sizeof(elf32_ehdr_t);   /* after ELF header */

    l->text_size = 16;
    l->text_off = off;  off += l->text_size;

    if (opts->include_reloc) {
        l->rela_size = sizeof(elf32_rela_t);  /* one entry */
        l->rela_off = off;  off += l->rela_size;
    } else {
        l->rela_off = 0; l->rela_size = 0;
    }

    if (opts->include_symtab) {
        l->sym_size = 3 * sizeof(elf32_sym_t);  /* null, app_init, host_export_a */
        l->sym_off = off;  off += l->sym_size;

        /* strtab: "\0" + optional "app_init\0" + "host_export_a\0" */
        l->str_size = 1 + (opts->include_app_init ? 9 : 0) + 15;
        l->str_off = off;  off += l->str_size;
    } else {
        l->sym_off = 0; l->sym_size = 0;
        l->str_off = 0; l->str_size = 0;
    }

    /* shstrtab: computed from opts */
    {
        size_t s = 1 + 6;                    /* "\0" + ".text\0" */
        if (opts->include_reloc)  s += 11;    /* ".rela.text\0" */
        if (opts->include_symtab) s += 8 + 8;/* ".symtab\0" + ".strtab\0" */
        s += 9;                              /* ".shstrtab\0" */
        l->shstr_size = (uint32_t)s;
    }
    l->shstr_off = off;  off += l->shstr_size;

    /* Align section header table to 4 bytes */
    off = (off + 3) & ~3u;
    l->shoff = off;

    /* Count sections */
    uint16_t n = 2;  /* null + .text */
    if (opts->include_reloc)   n++;  /* .rela.text */
    if (opts->include_symtab) n += 2;  /* .symtab + .strtab */
    n++;  /* .shstrtab */
    l->shnum = n;
}

/* Total file size. */
static uint32_t total_size(const layout_t *l) {
    return l->shoff + l->shnum * sizeof(elf32_shdr_t);
}

char *elf_fixture_build(const elf_fixture_opts_t *opts) {
    if (!opts) opts = &((elf_fixture_opts_t){.valid_magic=true,.machine=94,
                    .include_symtab=true,.include_app_init=true,.include_reloc=true});

    layout_t l;
    compute_layout(&l, opts);
    uint32_t fsize = total_size(&l);

    uint8_t *buf = calloc(1, fsize);
    if (!buf) return NULL;

    /* ---- ELF header ---- */
    elf32_ehdr_t *eh = (elf32_ehdr_t *)buf;
    eh->e_magic = opts->valid_magic ? ELF_MAGIC : 0xDEADBEEF;
    eh->e_class = 1;         /* ELF32 */
    eh->e_data = 1;          /* little-endian */
    eh->e_ident_ver = 1;
    eh->e_type = 2;          /* ET_EXEC */
    eh->e_machine = opts->machine;
    eh->e_version = 1;
    eh->e_entry = 0;
    eh->e_phoff = 0;
    eh->e_shoff = l.shoff;
    eh->e_flags = 0;
    eh->e_ehsize = sizeof(elf32_ehdr_t);
    eh->e_phentsize = 0;
    eh->e_phnum = 0;
    eh->e_shentsize = sizeof(elf32_shdr_t);
    eh->e_shnum = l.shnum;
    /* e_shstrndx = index of .shstrtab (last section) */
    eh->e_shstrndx = l.shnum - 1;

    /* ---- .text data: 16 bytes of dummy code ---- */
    /* The first 4 bytes will be patched by the relocation. Pre-fill with 0. */
    if (opts->include_reloc) {
        memset(buf + l.text_off, 0, l.text_size);
    } else {
        memset(buf + l.text_off, 0xCC, l.text_size);
    }

    /* ---- .rela.text: one relocation ---- */
    if (opts->include_reloc) {
        elf32_rela_t *rela = (elf32_rela_t *)(buf + l.rela_off);
        /* r_offset: within .text section (sh_addr=0x1000, so offset 0x1000) */
        uint32_t text_vma = 0x1000;
        rela->r_offset = opts->reloc_oob ? (text_vma + l.text_size + 100) : text_vma;
        /* r_sym=2 (host_export_a), r_type=R_XTENSA_32 */
        rela->r_info = (2u << 8) | R_XTENSA_32;
        rela->r_addend = 0;
    }

    /* ---- .symtab ---- */
    if (opts->include_symtab) {
        elf32_sym_t *syms = (elf32_sym_t *)(buf + l.sym_off);
        /* Index 0: null symbol (all zero, required) */
        memset(&syms[0], 0, sizeof(elf32_sym_t));

        int name_off = 1;  /* after initial null byte */

        /* Index 1: app_init (local, defined in .text section index 1) */
        if (opts->include_app_init) {
            syms[1].st_name = name_off;
            syms[1].st_value = 0x1000;  /* VMA within .text */
            syms[1].st_size = 4;
            syms[1].st_info = 0x12;     /* STB_GLOBAL | STT_FUNC */
            syms[1].st_other = 0;
            syms[1].st_shndx = 1;       /* points to .text section */
            name_off += 9;  /* "app_init\0" */
        } else {
            /* If no app_init, still need a symbol at index 1 for the reloc's
             * r_sym=2 to be in bounds. Make it a dummy local. */
            syms[1].st_name = name_off;
            syms[1].st_value = 0;
            syms[1].st_size = 0;
            syms[1].st_info = 0;
            syms[1].st_other = 0;
            syms[1].st_shndx = 0;
            name_off += 1;  /* just skip past the name byte */
        }

        /* Index 2: host_export_a (undefined, external — resolved via os_symtab) */
        syms[2].st_name = name_off;
        syms[2].st_value = 0;
        syms[2].st_size = 0;
        syms[2].st_info = 0x10;     /* STB_GLOBAL | STT_NOTYPE */
        syms[2].st_other = 0;
        syms[2].st_shndx = 0;       /* SHN_UNDEF */
    }

    /* ---- .strtab ---- */
    if (opts->include_symtab) {
        build_strtab((char *)(buf + l.str_off), opts->include_app_init);
    }

    /* ---- .shstrtab ---- */
    build_shstrtab((char *)(buf + l.shstr_off), opts->include_reloc, opts->include_symtab);

    /* ---- Section headers ---- */
    elf32_shdr_t *sh = (elf32_shdr_t *)(buf + l.shoff);
    int si = 0;  /* section index */

    /* Section 0: null */
    memset(&sh[si], 0, sizeof(elf32_shdr_t));
    si++;

    /* Section 1: .text */
    {
        /* Find ".text" in shstrtab */
        const char *shstr = (char *)(buf + l.shstr_off);
        sh[si].sh_name = (uint32_t)(strchr(shstr + 1, '.') - shstr);  /* offset of ".text" */
        sh[si].sh_type = SHT_PROGBITS;
        sh[si].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        sh[si].sh_addr = 0x1000;       /* VMA */
        sh[si].sh_offset = l.text_off;
        sh[si].sh_size = l.text_size;
        sh[si].sh_link = 0;
        sh[si].sh_info = 0;
        sh[si].sh_addralign = 4;
        sh[si].sh_entsize = 0;
        si++;
    }

    /* Section 2: .rela.text (optional) */
    if (opts->include_reloc) {
        const char *shstr = (char *)(buf + l.shstr_off);
        sh[si].sh_name = strstr(shstr, ".rela") - shstr;
        sh[si].sh_type = SHT_RELA;
        sh[si].sh_flags = 0;  /* Info only, not loaded */
        sh[si].sh_addr = 0;
        sh[si].sh_offset = l.rela_off;
        sh[si].sh_size = l.rela_size;
        sh[si].sh_link = opts->include_symtab ? 3 : 0;  /* symtab section index */
        sh[si].sh_info = 1;    /* targets .text (section index 1) */
        sh[si].sh_addralign = 4;
        sh[si].sh_entsize = sizeof(elf32_rela_t);
        si++;
    }

    /* Section 3: .symtab, Section 4: .strtab (optional) */
    if (opts->include_symtab) {
        const char *shstr = (char *)(buf + l.shstr_off);
        /* .symtab */
        sh[si].sh_name = strstr(shstr, ".symtab") - shstr;
        sh[si].sh_type = SHT_SYMTAB;
        sh[si].sh_flags = 0;
        sh[si].sh_addr = 0;
        sh[si].sh_offset = l.sym_off;
        sh[si].sh_size = l.sym_size;
        sh[si].sh_link = si + 1;  /* strtab is the next section */
        sh[si].sh_info = 1;       /* first non-local symbol index */
        sh[si].sh_addralign = 4;
        sh[si].sh_entsize = sizeof(elf32_sym_t);
        int symtab_idx = si;
        si++;

        /* .strtab */
        sh[si].sh_name = strstr(shstr, ".strtab") - shstr;
        sh[si].sh_type = SHT_STRTAB;
        sh[si].sh_flags = 0;
        sh[si].sh_addr = 0;
        sh[si].sh_offset = l.str_off;
        sh[si].sh_size = l.str_size;
        sh[si].sh_link = 0;
        sh[si].sh_info = 0;
        sh[si].sh_addralign = 1;
        sh[si].sh_entsize = 0;
        si++;
        (void)symtab_idx;
    }

    /* Last section: .shstrtab */
    {
        const char *shstr = (char *)(buf + l.shstr_off);
        sh[si].sh_name = strstr(shstr, ".shstrtab") - shstr;
        sh[si].sh_type = SHT_STRTAB;
        sh[si].sh_flags = 0;
        sh[si].sh_addr = 0;
        sh[si].sh_offset = l.shstr_off;
        sh[si].sh_size = l.shstr_size;
        sh[si].sh_link = 0;
        sh[si].sh_info = 0;
        sh[si].sh_addralign = 1;
        sh[si].sh_entsize = 0;
        si++;
    }

    /* ---- Write to temp file ---- */
    char tmpl[] = "/tmp/elf_fixture_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(buf); return NULL; }
    ssize_t wr = write(fd, buf, fsize);
    close(fd);
    free(buf);
    if (wr != (ssize_t)fsize) { unlink(tmpl); return NULL; }

    char *path = strdup(tmpl);
    return path;
}

char *elf_fixture_valid(void)       { return elf_fixture_build(&ELF_FIXTURE_VALID); }
char *elf_fixture_bad_magic(void)   { elf_fixture_opts_t o = ELF_FIXTURE_VALID; o.valid_magic = false; return elf_fixture_build(&o); }
char *elf_fixture_wrong_machine(void){ elf_fixture_opts_t o = ELF_FIXTURE_VALID; o.machine = 243; return elf_fixture_build(&o); }
char *elf_fixture_no_symtab(void)   { elf_fixture_opts_t o = ELF_FIXTURE_VALID; o.include_symtab = false; return elf_fixture_build(&o); }
char *elf_fixture_no_app_init(void) { elf_fixture_opts_t o = ELF_FIXTURE_VALID; o.include_app_init = false; return elf_fixture_build(&o); }
