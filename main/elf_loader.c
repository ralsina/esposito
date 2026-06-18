#include "elf_loader.h"
#include "elf_validate.h"
#include "os_symtab.h"
#include "os_core.h"
#include "app_heap.h"
#include "sd_card.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "elf_loader";

#define ET_EXEC   2

#define PT_LOAD   1
#define PF_X      1
#define PF_W      2
#define PF_R      4

#define SHT_RELA  4
#define SHT_REL   9
#define SHT_NOBITS 8
#define SHF_ALLOC 2
#define SHF_WRITE 1
#define SHF_EXECINSTR 4

#define R_XTENSA_32        1
#define R_XTENSA_SLOT0_OP  7
#define R_XTENSA_ASM_EXPAND 10

#define R_RISCV_32         1
#define R_RISCV_HI20       26
#define R_RISCV_LO12_I     27
#define R_RISCV_LO12_S     28
#define R_RISCV_CALL       18

#if CONFIG_IDF_TARGET_ESP32C3
#define EXPECTED_RELOC_TYPE R_RISCV_32
#else
#define EXPECTED_RELOC_TYPE R_XTENSA_32
#endif

#define APP_PARTITION_LABEL "app_code"

/* ELF32 on-disk struct definitions (elf32_ehdr_t, elf32_shdr_t, elf32_sym_t)
 * and the ELF_MAGIC/EM_* constants live in elf_validate.h, shared with the
 * host-side validation tests. */

typedef struct __attribute__((packed)) {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} elf32_rela_t;

typedef struct __attribute__((packed)) {
    uint32_t r_offset;
    uint32_t r_info;
} elf32_rel_t;

typedef struct {
    uint32_t vma;
    uint32_t size;
    uint32_t load_addr;
    uint32_t flags;
} loaded_section_t;

#define MAX_LOADED_SECTIONS 16

struct elf_handle {
    char name[64];

    void (*init)(app_context_t *ctx);
    void (*checkpoint)(app_context_t *ctx);
    void (*close)(app_context_t *ctx);
    void (*event_fn)(app_context_t *ctx, event_t *event);

    const esp_partition_t *flash_part;
    esp_partition_mmap_handle_t inst_mmap_handle;
    esp_partition_mmap_handle_t data_mmap_handle;
    void *inst_base;
    void *data_base_flash;

    void *data_base;
    size_t data_size;
    void *bss_base;
    size_t bss_size;

    loaded_section_t sections[MAX_LOADED_SECTIONS];
    int section_count;
};

static bool read_exact_at(FILE *fp, long offset, void *buffer, size_t size) {
    if (fseek(fp, offset, SEEK_SET) != 0) {
        return false;
    }
    return fread(buffer, 1, size, fp) == size;
}

static loaded_section_t *find_loaded_section_by_vma(elf_handle_t *handle, uint32_t vma) {
    for (int index = 0; index < handle->section_count; index++) {
        if (handle->sections[index].vma == vma) {
            return &handle->sections[index];
        }
    }
    return NULL;
}

static uint32_t elf_resolve_local_symbol(elf_handle_t *handle, const elf32_sym_t *sym) {
    if (sym->st_shndx == 0 || sym->st_shndx == 0xFFF1 /* SHN_ABS */ || sym->st_value == 0) return sym->st_value;
    for (int i = 0; i < handle->section_count; i++) {
        uint32_t sec_end = handle->sections[i].vma + handle->sections[i].size;
        if (sym->st_value >= handle->sections[i].vma && sym->st_value < sec_end) {
            return handle->sections[i].load_addr + (sym->st_value - handle->sections[i].vma);
        }
    }
    return sym->st_value;
}

static bool elf_alloc_data_bss(elf_handle_t *handle,
                                FILE *fp,
                                const elf32_ehdr_t *ehdr,
                                const elf32_shdr_t *shdrs) {
    handle->data_base = NULL;
    handle->data_size = 0;
    handle->bss_base = NULL;
    handle->bss_size = 0;

    uint32_t data_load = 0, bss_size = 0;
    const elf32_shdr_t *first_data_shdr = NULL;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        const elf32_shdr_t *sh = &shdrs[i];
        if (!(sh->sh_flags & SHF_ALLOC)) continue;

        if (sh->sh_flags & SHF_WRITE) {
            if (sh->sh_type != SHT_NOBITS) {
                data_load += sh->sh_size;
                if (!first_data_shdr) first_data_shdr = sh;
            } else {
                bss_size += sh->sh_size;
            }
        }
    }

    if (data_load > 0) {
        handle->data_base = app_malloc(data_load);
        if (!handle->data_base) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for .data", data_load);
            return false;
        }
        handle->data_size = data_load;
        memset(handle->data_base, 0, data_load);

        uint32_t data_offset = 0;
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const elf32_shdr_t *sh = &shdrs[i];
            if (!(sh->sh_flags & SHF_ALLOC)) continue;
            if (!(sh->sh_flags & SHF_WRITE)) continue;
            if (sh->sh_type == SHT_NOBITS) continue;

            if (!read_exact_at(fp, sh->sh_offset, (uint8_t *)handle->data_base + data_offset, sh->sh_size)) {
                ESP_LOGE(TAG, "Failed to read .data section at offset 0x%lx", (unsigned long)sh->sh_offset);
                return false;
            }
            data_offset += sh->sh_size;

            if (handle->section_count >= MAX_LOADED_SECTIONS) {
                ESP_LOGW(TAG, "MAX_LOADED_SECTIONS (%d) reached; remaining .data sections not tracked", MAX_LOADED_SECTIONS);
                break;
            }
            loaded_section_t *ls = &handle->sections[handle->section_count++];
            ls->vma = sh->sh_addr;
            ls->size = sh->sh_size;
            ls->load_addr = (uint32_t)handle->data_base + (data_offset - sh->sh_size);
            ls->flags = sh->sh_flags;

            ESP_LOGD(TAG, "  data section: vma=0x%x size=%d load=0x%lx",
                     ls->vma, ls->size, (unsigned long)ls->load_addr);
        }
    }

    if (bss_size > 0) {
        handle->bss_base = app_calloc(1, bss_size);
        if (!handle->bss_base) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for .bss", bss_size);
            return false;
        }
        handle->bss_size = bss_size;

        uint32_t bss_offset = 0;
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const elf32_shdr_t *sh = &shdrs[i];
            if (!(sh->sh_flags & SHF_ALLOC)) continue;
            if (!(sh->sh_flags & SHF_WRITE)) continue;
            if (sh->sh_type != SHT_NOBITS) continue;

            if (handle->section_count >= MAX_LOADED_SECTIONS) {
                ESP_LOGW(TAG, "MAX_LOADED_SECTIONS (%d) reached; remaining .bss sections not tracked", MAX_LOADED_SECTIONS);
                break;
            }
            loaded_section_t *ls = &handle->sections[handle->section_count++];
            ls->vma = sh->sh_addr;
            ls->size = sh->sh_size;
            ls->load_addr = (uint32_t)handle->bss_base + bss_offset;
            ls->flags = sh->sh_flags;

            bss_offset += sh->sh_size;
        }
    }

    return true;
}

static bool apply_relocations_for_target(elf_handle_t *handle,
                                         FILE *fp,
                                         const elf32_ehdr_t *ehdr,
                                         const elf32_shdr_t *shdrs,
                                         const elf32_sym_t *symtab,
                                         int sym_count,
                                         const char *strtab,
                                         int target_section_idx,
                                         void *patch_base,
                                         int *patched_count) {
    if (target_section_idx < 0 || target_section_idx >= ehdr->e_shnum) {
        return true;  // nothing to do
    }
    const elf32_shdr_t *target_sh = &shdrs[target_section_idx];

    for (int section_index = 0; section_index < ehdr->e_shnum; section_index++) {
        const elf32_shdr_t *rel_sh = &shdrs[section_index];
        if (rel_sh->sh_type != SHT_RELA && rel_sh->sh_type != SHT_REL) {
            continue;
        }
        if (rel_sh->sh_info >= ehdr->e_shnum ||
            (int)rel_sh->sh_info != target_section_idx ||
            rel_sh->sh_size == 0) {
            continue;
        }

        int rel_size = (rel_sh->sh_type == SHT_RELA) ? (int)sizeof(elf32_rela_t) : (int)sizeof(elf32_rel_t);
        if (rel_size == 0) {
            continue;
        }

        int num_rels = rel_sh->sh_size / rel_size;

        for (int rel_index = 0; rel_index < num_rels; rel_index++) {
            uint32_t r_offset, r_info, r_sym, r_type;
            int32_t r_addend = 0;

            if (rel_sh->sh_type == SHT_RELA) {
                elf32_rela_t rel;
                long rel_off = (long)rel_sh->sh_offset + (long)rel_index * rel_size;
                if (!read_exact_at(fp, rel_off, &rel, sizeof(rel))) {
                    ESP_LOGE(TAG, "Failed to read RELA entry %d at offset 0x%lx",
                             rel_index, (unsigned long)rel_off);
                    return false;
                }
                r_offset = rel.r_offset;
                r_info = rel.r_info;
                r_addend = rel.r_addend;
            } else {
                elf32_rel_t rel;
                long rel_off = (long)rel_sh->sh_offset + (long)rel_index * rel_size;
                if (!read_exact_at(fp, rel_off, &rel, sizeof(rel))) {
                    ESP_LOGE(TAG, "Failed to read REL entry %d at offset 0x%lx",
                             rel_index, (unsigned long)rel_off);
                    return false;
                }
                r_offset = rel.r_offset;
                r_info = rel.r_info;
            }

            r_sym = r_info >> 8;
            r_type = r_info & 0xFF;

            if (r_type != EXPECTED_RELOC_TYPE || !symtab || r_sym == 0) {
                continue;
            }
            if (!elf_reloc_sym_in_bounds(r_sym, sym_count)) {
                ESP_LOGW(TAG, "Relocation symbol index %u out of range (sym_count=%d), skipping",
                         r_sym, sym_count);
                continue;
            }

            if (!elf_reloc_offset_in_bounds(r_offset, target_sh)) {
                continue;
            }

            const elf32_sym_t *sym = &symtab[r_sym];
            uint32_t *patch_addr = (uint32_t *)((uint8_t *)patch_base + (r_offset - target_sh->sh_addr));
            uint32_t old_val = *patch_addr;
            uint32_t new_val;

            if (sym->st_shndx == 0 || sym->st_shndx == 0xFFF1 /* SHN_ABS */) {
                const char *sym_name = strtab ? strtab + sym->st_name : "?";
                const os_symtab_entry_t *entry = os_symtab_lookup(sym_name);
                if (!entry) {
                    ESP_LOGW(TAG, "Undefined symbol: %s", sym_name);
                    continue;
                }
                new_val = (uint32_t)(uintptr_t)entry->addr + r_addend;
            } else {
                uint32_t runtime_addr = elf_resolve_local_symbol(handle, sym);
                new_val = old_val + (runtime_addr - sym->st_value);
            }

            *patch_addr = new_val;
            (*patched_count)++;
        }
    }

    return true;
}

static bool elf_resolve_entry_points(elf_handle_t *handle,
                                      const elf32_ehdr_t *ehdr,
                                      const elf32_shdr_t *shdrs,
                                      const elf32_sym_t *symtab,
                                      const char *strtab) {
    if (!symtab || !strtab) return false;

    int symtab_section = -1;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdrs[i].sh_type == 2) {
            symtab_section = i;
            break;
        }
    }
    if (symtab_section < 0) return false;

    const elf32_sym_t *syms = symtab;
    int num_syms = shdrs[symtab_section].sh_size / sizeof(elf32_sym_t);

    handle->init = NULL;
    handle->checkpoint = NULL;
    handle->close = NULL;
    handle->event_fn = NULL;

    for (int i = 0; i < num_syms; i++) {
        const elf32_sym_t *sym = &syms[i];
        const char *name = strtab + sym->st_name;

        if (sym->st_value == 0) continue;

        uint32_t runtime_addr = elf_resolve_local_symbol(handle, sym);

        void *func_addr = (void *)runtime_addr;

        if (strcmp(name, "app_init") == 0) {
            handle->init = func_addr;
            ESP_LOGI(TAG, "app_init -> %p", func_addr);
        } else if (strcmp(name, "app_checkpoint") == 0) {
            handle->checkpoint = func_addr;
        } else if (strcmp(name, "app_close") == 0) {
            handle->close = func_addr;
        } else if (strcmp(name, "app_event") == 0) {
            handle->event_fn = func_addr;
        }
    }

    if (!handle->init) {
        ESP_LOGE(TAG, "app_init not found in ELF");
        return false;
    }

    ESP_LOGI(TAG, "Entry points: init=%p checkpoint=%p close=%p event=%p",
             handle->init, handle->checkpoint, handle->close, handle->event_fn);
    return true;
}

elf_handle_t *elf_loader_load(const char *path) {
    ESP_LOGI(TAG, "Loading ELF: %s", path);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return NULL;
    }

    // All resources tracked here are freed either on the success path (returning handle)
    // or via the `fail:` label at the bottom. Initialize to NULL so cleanup is uniform.
    elf32_shdr_t *shdrs = NULL;
    elf32_sym_t *symtab = NULL;
    char *strtab = NULL;
    elf_handle_t *handle = NULL;
    int sym_count = 0;

    elf32_ehdr_t ehdr_storage;
    if (!read_exact_at(fp, 0, &ehdr_storage, sizeof(ehdr_storage))) {
        ESP_LOGE(TAG, "Failed to read ELF header");
        goto fail;
    }
    const elf32_ehdr_t *ehdr = &ehdr_storage;

    uint16_t expected_machine;
#if CONFIG_IDF_TARGET_ESP32C3
    expected_machine = EM_RISCV;
#else
    expected_machine = EM_XTENSA;
#endif

    const char *hdr_err = NULL;
    if (!elf_validate_header(ehdr, expected_machine, &hdr_err)) {
        ESP_LOGE(TAG, "Invalid ELF header (%s): magic=0x%x machine=%d expected=%d "
                 "shnum=%u shentsize=%u shstrndx=%u",
                 hdr_err ? hdr_err : "unknown", ehdr->e_magic, ehdr->e_machine,
                 expected_machine, ehdr->e_shnum, ehdr->e_shentsize, ehdr->e_shstrndx);
        goto fail;
    }

    shdrs = app_malloc(sizeof(elf32_shdr_t) * ehdr->e_shnum);
    if (!shdrs) {
        ESP_LOGE(TAG, "Failed to allocate section headers");
        goto fail;
    }
    if (!read_exact_at(fp, ehdr->e_shoff, shdrs, sizeof(elf32_shdr_t) * ehdr->e_shnum)) {
        ESP_LOGE(TAG, "Failed to read section headers");
        goto fail;
    }

    int symtab_section = -1;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdrs[i].sh_type == 2) {
            symtab_section = i;
            break;
        }
    }
    if (symtab_section < 0) {
        ESP_LOGE(TAG, "No symbol table in ELF");
        goto fail;
    }

    const elf32_shdr_t *sym_sh = &shdrs[symtab_section];
    // Cap symtab/strtab sizes to defend against a corrupted ELF claiming a huge table.
    // 1 MB is far above what any real app needs (typical is <100 KB) but bounded enough
    // that a malformed file cannot OOM the device.
    const elf32_shdr_t *str_sh = NULL;
    if (sym_sh->sh_link > 0 && sym_sh->sh_link < ehdr->e_shnum) {
        str_sh = &shdrs[sym_sh->sh_link];
    }
    const char *sym_err = NULL;
    if (!elf_validate_symtab(sym_sh, str_sh, &sym_err)) {
        ESP_LOGE(TAG, "Symbol table rejected (%s): sym_size=%u str_size=%u",
                 sym_err ? sym_err : "unknown", sym_sh->sh_size,
                 str_sh ? str_sh->sh_size : 0);
        goto fail;
    }

    symtab = app_malloc(sym_sh->sh_size);
    strtab = str_sh ? app_malloc(str_sh->sh_size) : NULL;
    if (!symtab || (str_sh && !strtab)) {
        ESP_LOGE(TAG, "Failed to allocate symbol/string tables");
        goto fail;
    }
    if (!read_exact_at(fp, sym_sh->sh_offset, symtab, sym_sh->sh_size) ||
        (str_sh && !read_exact_at(fp, str_sh->sh_offset, strtab, str_sh->sh_size))) {
        ESP_LOGE(TAG, "Failed to read symbol/string tables");
        goto fail;
    }
    sym_count = sym_sh->sh_size / sizeof(elf32_sym_t);

    handle = app_calloc(1, sizeof(elf_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate ELF handle");
        goto fail;
    }

    const char *name = strrchr(path, '/');
    strncpy(handle->name, name ? name + 1 : path, sizeof(handle->name) - 1);

    // Step 1: Find and erase the app partition
    handle->flash_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                   ESP_PARTITION_SUBTYPE_DATA_UNDEFINED,
                                                   APP_PARTITION_LABEL);
    if (!handle->flash_part) {
        ESP_LOGE(TAG, "App partition not found");
        goto fail;
    }

    esp_err_t ret = esp_partition_erase_range(handle->flash_part, 0, handle->flash_part->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase app partition: %s", esp_err_to_name(ret));
        goto fail;
    }

    // Step 2: Calculate code/rodata sizes
    size_t code_size = 0, rodata_size = 0;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        const elf32_shdr_t *sh = &shdrs[i];
        if (!(sh->sh_flags & SHF_ALLOC)) continue;
        if (sh->sh_flags & SHF_WRITE) continue;
        if (sh->sh_type == SHT_NOBITS || sh->sh_size == 0) continue;
        if (sh->sh_flags & SHF_EXECINSTR) {
            code_size += (sh->sh_size + 3) & ~3;
        } else {
            rodata_size += (sh->sh_size + 3) & ~3;
        }
    }

    // Step 3: mmap code into IROM (for execution) and rodata into DROM (for data access)
    // .text goes to IROM (instruction cache), .rodata goes to DROM (data cache).
    // This is required because on ESP32, data loads from IROM cause LoadStoreError.
    ret = esp_partition_mmap(handle->flash_part, 0, code_size,
                              ESP_PARTITION_MMAP_INST,
                              (const void **)&handle->inst_base,
                              &handle->inst_mmap_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap code: %s", esp_err_to_name(ret));
        goto fail;
    }

    if (rodata_size > 0) {
        ret = esp_partition_mmap(handle->flash_part, code_size, rodata_size,
                                  ESP_PARTITION_MMAP_DATA,
                                  (const void **)&handle->data_base_flash,
                                  &handle->data_mmap_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mmap rodata: %s", esp_err_to_name(ret));
            goto fail;
        }
    } else {
        handle->data_base_flash = NULL;
        handle->data_mmap_handle = 0;
    }

    ESP_LOGI(TAG, "IROM base: %p, DROM base: %p, code=%d rodata=%d",
             handle->inst_base, handle->data_base_flash, code_size, rodata_size);

    // Step 4: Build runtime section table for code/rodata
    {
        uint32_t co_off = 0, ro_off = 0;
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const elf32_shdr_t *sh = &shdrs[i];
            if (!(sh->sh_flags & SHF_ALLOC)) continue;
            if (sh->sh_flags & SHF_WRITE) continue;
            if (handle->section_count >= MAX_LOADED_SECTIONS) {
                ESP_LOGW(TAG, "MAX_LOADED_SECTIONS (%d) reached; remaining code/rodata sections not tracked (relocations may fail)",
                         MAX_LOADED_SECTIONS);
                break;
            }

            loaded_section_t *ls = &handle->sections[handle->section_count++];
            ls->vma = sh->sh_addr;
            ls->size = sh->sh_size;
            ls->flags = sh->sh_flags;

            if (sh->sh_flags & SHF_EXECINSTR) {
                ls->load_addr = (uint32_t)handle->inst_base + co_off;
                if (sh->sh_type != SHT_NOBITS) co_off += (sh->sh_size + 3) & ~3;
            } else {
                ls->load_addr = (uint32_t)handle->data_base_flash + ro_off;
                if (sh->sh_type != SHT_NOBITS) ro_off += (sh->sh_size + 3) & ~3;
            }
            ESP_LOGD(TAG, "  section[%d]: vma=0x%x size=%d load=0x%lx flags=0x%x",
                     handle->section_count-1, ls->vma, ls->size, (unsigned long)ls->load_addr, ls->flags);
        }
    }


    // Step 5: Allocate DRAM for data/bss NOW, so relocations can resolve their addresses
    if (!elf_alloc_data_bss(handle, fp, ehdr, shdrs)) {
        ESP_LOGE(TAG, "Failed to allocate data/bss");
        goto fail;
    }
    ESP_LOGI(TAG, "DRAM: data=%p size=%d bss=%p size=%d",
             handle->data_base, handle->data_size, handle->bss_base, handle->bss_size);

    // Step 6: Relocate + write flash sections one by one (no full ELF buffer)
    int total_patched = 0;
    for (int pass = 0; pass < 2; pass++) {
        size_t write_offset = (pass == 0) ? 0 : code_size;
        for (int section_index = 0; section_index < ehdr->e_shnum; section_index++) {
            const elf32_shdr_t *sh = &shdrs[section_index];
            if (!(sh->sh_flags & SHF_ALLOC)) continue;
            if (sh->sh_flags & SHF_WRITE) continue;
            if (sh->sh_type == SHT_NOBITS || sh->sh_size == 0) continue;

            bool is_exec = (sh->sh_flags & SHF_EXECINSTR) != 0;
            if ((pass == 0 && !is_exec) || (pass == 1 && is_exec)) continue;

            // Use app heap for large per-section temp buffers to avoid global heap fragmentation.
            uint8_t *section_buf = app_malloc(sh->sh_size);
            if (!section_buf) {
                ESP_LOGE(TAG, "Failed to allocate section buffer (%lu bytes)", (unsigned long)sh->sh_size);
                goto fail;
            }

            if (!read_exact_at(fp, sh->sh_offset, section_buf, sh->sh_size)) {
                ESP_LOGE(TAG, "Failed to read section data at offset 0x%lx", (unsigned long)sh->sh_offset);
                app_free(section_buf);
                goto fail;
            }

            if (!apply_relocations_for_target(handle, fp, ehdr, shdrs, symtab, sym_count, strtab,
                                              section_index, section_buf, &total_patched)) {
                app_free(section_buf);
                goto fail;
            }

            ret = esp_partition_write(handle->flash_part, write_offset, section_buf, sh->sh_size);
            app_free(section_buf);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write to flash: %s", esp_err_to_name(ret));
                goto fail;
            }

            write_offset += (sh->sh_size + 3) & ~3;
        }
    }

    // Step 7: Apply relocations to DRAM targets (.data/.bss)
    for (int section_index = 0; section_index < ehdr->e_shnum; section_index++) {
        const elf32_shdr_t *target_sh = &shdrs[section_index];
        if (!(target_sh->sh_flags & SHF_ALLOC)) continue;
        if (!(target_sh->sh_flags & SHF_WRITE)) continue;

        loaded_section_t *ls = find_loaded_section_by_vma(handle, target_sh->sh_addr);
        if (!ls) {
            continue;
        }

        if (!apply_relocations_for_target(handle, fp, ehdr, shdrs, symtab, sym_count, strtab,
                                          section_index, (void *)ls->load_addr, &total_patched)) {
            goto fail;
        }
    }

    ESP_LOGI(TAG, "Patched %d relocations", total_patched);

    // Step 8: Invalidate cache for both IROM and DROM mappings
    esp_cache_msync(handle->inst_base, code_size, ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    if (handle->data_base_flash && rodata_size > 0) {
        esp_cache_msync(handle->data_base_flash, rodata_size, ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    }

    // Step 9: Resolve entry points
    if (!elf_resolve_entry_points(handle, ehdr, shdrs, symtab, strtab)) {
        ESP_LOGE(TAG, "Failed to resolve entry points");
        goto fail;
    }

    app_free(strtab);
    app_free(symtab);
    app_free(shdrs);
    fclose(fp);
    ESP_LOGI(TAG, "ELF loaded: %s", handle->name);
    return handle;

fail:
    app_free(strtab);
    app_free(symtab);
    app_free(shdrs);
    if (handle) elf_loader_unload(handle);
    fclose(fp);
    return NULL;
}

void elf_loader_unload(elf_handle_t *handle) {
    if (!handle) return;

    if (handle->inst_mmap_handle) {
        esp_partition_munmap(handle->inst_mmap_handle);
    }
    if (handle->data_mmap_handle) {
        esp_partition_munmap(handle->data_mmap_handle);
        handle->data_mmap_handle = 0;
    }
    if (handle->data_base) {
        app_free(handle->data_base);
    }
    if (handle->bss_base) {
        app_free(handle->bss_base);
    }
    app_free(handle);
}

void *elf_loader_symbol(elf_handle_t *handle, const char *name) {
    if (!handle || !name) return NULL;
    if (strcmp(name, "app_init") == 0) return (void *)handle->init;
    if (strcmp(name, "app_checkpoint") == 0) return (void *)handle->checkpoint;
    if (strcmp(name, "app_close") == 0) return (void *)handle->close;
    if (strcmp(name, "app_event") == 0) return (void *)handle->event_fn;
    return NULL;
}
