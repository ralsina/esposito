#include "elf_loader.h"
#include "os_symtab.h"
#include "os_core.h"
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

#define ELF_MAGIC 0x464C457F
#define EM_XTENSA 94
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

#define APP_PARTITION_LABEL "app_code"

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

static uint32_t elf_resolve_local_symbol(elf_handle_t *handle, const elf32_sym_t *sym) {
    if (sym->st_shndx == 0 || sym->st_value == 0) return sym->st_value;
    for (int i = 0; i < handle->section_count; i++) {
        uint32_t sec_end = handle->sections[i].vma + handle->sections[i].size;
        if (sym->st_value >= handle->sections[i].vma && sym->st_value < sec_end) {
            return handle->sections[i].load_addr + (sym->st_value - handle->sections[i].vma);
        }
    }
    return sym->st_value;
}

static bool elf_alloc_data_bss(elf_handle_t *handle,
                                const uint8_t *elf_data,
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
        handle->data_base = malloc(data_load);
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

            memcpy((uint8_t *)handle->data_base + data_offset,
                   elf_data + sh->sh_offset, sh->sh_size);
            data_offset += sh->sh_size;

            if (handle->section_count >= MAX_LOADED_SECTIONS) break;
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
        handle->bss_base = calloc(1, bss_size);
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

            if (handle->section_count >= MAX_LOADED_SECTIONS) break;
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

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *elf_data = malloc(file_size);
    if (!elf_data) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for ELF", file_size);
        fclose(fp);
        return NULL;
    }

    if (fread(elf_data, 1, file_size, fp) != (size_t)file_size) {
        ESP_LOGE(TAG, "Failed to read ELF file");
        free(elf_data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;
    if (ehdr->e_magic != ELF_MAGIC || ehdr->e_machine != EM_XTENSA) {
        ESP_LOGE(TAG, "Invalid ELF (magic=0x%x machine=%d)", ehdr->e_magic, ehdr->e_machine);
        free(elf_data);
        return NULL;
    }

    const elf32_shdr_t *shdrs = (const elf32_shdr_t *)(elf_data + ehdr->e_shoff);
    const elf32_sym_t *symtab = NULL;
    const char *strtab = NULL;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdrs[i].sh_type == 2) {
            symtab = (const elf32_sym_t *)(elf_data + shdrs[i].sh_offset);
            if (shdrs[i].sh_link < ehdr->e_shnum) {
                const elf32_shdr_t *str_sh = &shdrs[shdrs[i].sh_link];
                strtab = (const char *)(elf_data + str_sh->sh_offset);
            }
            break;
        }
    }

    elf_handle_t *handle = calloc(1, sizeof(elf_handle_t));
    if (!handle) {
        free(elf_data);
        return NULL;
    }

    const char *name = strrchr(path, '/');
    strncpy(handle->name, name ? name + 1 : path, sizeof(handle->name) - 1);

    // Step 1: Find and erase the app partition
    handle->flash_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                   ESP_PARTITION_SUBTYPE_DATA_UNDEFINED,
                                                   APP_PARTITION_LABEL);
    if (!handle->flash_part) {
        ESP_LOGE(TAG, "App partition not found");
        free(elf_data);
        free(handle);
        return NULL;
    }

    esp_err_t ret = esp_partition_erase_range(handle->flash_part, 0, handle->flash_part->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase app partition: %s", esp_err_to_name(ret));
        free(elf_data);
        free(handle);
        return NULL;
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

    // Step 3: mmap erased partition (empty — just reserves virtual address space)
    ret = esp_partition_mmap(handle->flash_part, 0, code_size,
                              ESP_PARTITION_MMAP_INST,
                              (const void **)&handle->inst_base,
                              &handle->inst_mmap_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap code: %s", esp_err_to_name(ret));
        free(elf_data);
        elf_loader_unload(handle);
        return NULL;
    }

    ret = esp_partition_mmap(handle->flash_part, code_size, rodata_size,
                              ESP_PARTITION_MMAP_DATA,
                              (const void **)&handle->data_base_flash,
                              &handle->data_mmap_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap rodata: %s", esp_err_to_name(ret));
        free(elf_data);
        elf_loader_unload(handle);
        return NULL;
    }

    ESP_LOGI(TAG, "Code IROM: %p, Rodata DROM: %p, total %d bytes",
             handle->inst_base, handle->data_base_flash, code_size + rodata_size);

    // Step 4: Build runtime section table for code/rodata (now we know runtime addresses)
    {
        uint32_t co_off = 0, ro_off = 0;
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const elf32_shdr_t *sh = &shdrs[i];
            if (!(sh->sh_flags & SHF_ALLOC)) continue;
            if (sh->sh_flags & SHF_WRITE) continue;
            if (handle->section_count >= MAX_LOADED_SECTIONS) break;

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
    if (!elf_alloc_data_bss(handle, elf_data, ehdr, shdrs)) {
        ESP_LOGE(TAG, "Failed to allocate data/bss");
        free(elf_data);
        elf_loader_unload(handle);
        return NULL;
    }
    ESP_LOGI(TAG, "DRAM: data=%p size=%d bss=%p size=%d",
             handle->data_base, handle->data_size, handle->bss_base, handle->bss_size);

    // Step 6: Apply ALL relocations (section table now has code + rodata + data + bss)
    int total_patched = 0;
    for (int rel_pass = 0; rel_pass < 2; rel_pass++) {
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const elf32_shdr_t *sh = &shdrs[i];
            if (sh->sh_type != SHT_RELA && sh->sh_type != SHT_REL) continue;

            int target_section_idx = sh->sh_info;
            if (target_section_idx < 0 || target_section_idx >= ehdr->e_shnum) continue;

            const elf32_shdr_t *target_sh = &shdrs[target_section_idx];
            if (!(target_sh->sh_flags & SHF_ALLOC)) continue;

            // Pass 0: flash sections (.text, .rodata), Pass 1: DRAM sections (.data)
            bool target_is_dram = (target_sh->sh_flags & SHF_WRITE) != 0;
            if ((rel_pass == 0 && target_is_dram) || (rel_pass == 1 && !target_is_dram)) continue;

            void *patch_base;
            if (target_is_dram) {
                // Find DRAM base for this target section
                patch_base = NULL;
                for (int s = 0; s < handle->section_count; s++) {
                    if (handle->sections[s].vma == target_sh->sh_addr) {
                        patch_base = (void *)handle->sections[s].load_addr;
                        break;
                    }
                }
                if (!patch_base) continue;
            } else {
                // Patch in the ELF data buffer (will be written to flash)
                patch_base = elf_data + target_sh->sh_offset;
            }

            int symtab_link = sh->sh_link;
            const elf32_sym_t *rel_symtab = symtab;
            const char *rel_strtab = strtab;
            if (symtab_link > 0 && symtab_link < ehdr->e_shnum) {
                const elf32_shdr_t *link_sh = &shdrs[symtab_link];
                if (link_sh->sh_type == 2)
                    rel_symtab = (const elf32_sym_t *)(elf_data + link_sh->sh_offset);
                if (link_sh->sh_link > 0) {
                    const elf32_shdr_t *str_sh = &shdrs[link_sh->sh_link];
                    rel_strtab = (const char *)(elf_data + str_sh->sh_offset);
                }
            }

            int num_rels;
            const void *rel_data;
            int rel_size;
            bool is_rela = (sh->sh_type == SHT_RELA);

            if (is_rela) {
                num_rels = sh->sh_size / sizeof(elf32_rela_t);
                rel_data = elf_data + sh->sh_offset;
                rel_size = sizeof(elf32_rela_t);
            } else {
                num_rels = sh->sh_size / sizeof(elf32_rel_t);
                rel_data = elf_data + sh->sh_offset;
                rel_size = sizeof(elf32_rel_t);
            }

            for (int j = 0; j < num_rels; j++) {
                uint32_t r_offset, r_info, r_sym, r_type;
                int32_t r_addend = 0;

                if (is_rela) {
                    const elf32_rela_t *r = (const elf32_rela_t *)((const uint8_t *)rel_data + j * rel_size);
                    r_offset = r->r_offset;
                    r_info = r->r_info;
                    r_sym = r_info >> 8;
                    r_type = r_info & 0xFF;
                    r_addend = r->r_addend;
                } else {
                    const elf32_rel_t *r = (const elf32_rel_t *)((const uint8_t *)rel_data + j * rel_size);
                    r_offset = r->r_offset;
                    r_info = r->r_info;
                    r_sym = r_info >> 8;
                    r_type = r_info & 0xFF;
                }

                if (r_type == R_XTENSA_32 && rel_symtab && r_sym > 0) {
                    const elf32_sym_t *sym = &rel_symtab[r_sym];
                    uint32_t old_val = *(uint32_t *)((uint8_t *)patch_base + (r_offset - target_sh->sh_addr));
                    uint32_t new_val;

                    if (sym->st_shndx == 0) {
                        const char *sym_name = rel_strtab ? rel_strtab + sym->st_name : "?";
                        const os_symtab_entry_t *entry = os_symtab_lookup(sym_name);
                        if (!entry) {
                            ESP_LOGW(TAG, "Undefined symbol: %s", sym_name);
                            continue;
                        }
                        new_val = (uint32_t)(uintptr_t)entry->addr + r_addend;
                    } else {
                        uint32_t runtime_addr = elf_resolve_local_symbol(handle, sym);
                        new_val = runtime_addr + r_addend;
                    }

                    uint32_t *patch_addr = (uint32_t *)((uint8_t *)patch_base + (r_offset - target_sh->sh_addr));
                    if (old_val != new_val) {
                        const char *sym_name = rel_strtab ? rel_strtab + sym->st_name : "?";
                        ESP_LOGD(TAG, "  patch[%d] sym=%s target=%s 0x%lx -> 0x%lx",
                                 total_patched, sym_name,
                                 target_is_dram ? "DRAM" : "FLASH",
                                 (unsigned long)old_val, (unsigned long)new_val);
                    }
                    *patch_addr = new_val;
                    total_patched++;
                }
            }
        }
    }
    ESP_LOGI(TAG, "Patched %d relocations", total_patched);

    // Step 7: Write patched code/rodata from buffer to flash (first write, erased — always works)
    {
        size_t w_off = 0;
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) w_off = code_size;
            for (int i = 0; i < ehdr->e_shnum; i++) {
                const elf32_shdr_t *sh = &shdrs[i];
                if (!(sh->sh_flags & SHF_ALLOC)) continue;
                if (sh->sh_flags & SHF_WRITE) continue;
                if (sh->sh_type == SHT_NOBITS || sh->sh_size == 0) continue;

                bool is_exec = (sh->sh_flags & SHF_EXECINSTR) != 0;
                if ((pass == 0 && !is_exec) || (pass == 1 && is_exec)) continue;

                ret = esp_partition_write(handle->flash_part, w_off,
                                           elf_data + sh->sh_offset, sh->sh_size);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to write to flash: %s", esp_err_to_name(ret));
                    free(elf_data);
                    elf_loader_unload(handle);
                    return NULL;
                }
                w_off += (sh->sh_size + 3) & ~3;
            }
        }
    }

    // Step 8: Invalidate cache so CPU reads fresh data from flash
    esp_cache_msync(handle->inst_base, handle->flash_part->size, ESP_CACHE_MSYNC_FLAG_INVALIDATE);

    // Step 9: Resolve entry points
    if (!elf_resolve_entry_points(handle, ehdr, shdrs, symtab, strtab)) {
        ESP_LOGE(TAG, "Failed to resolve entry points");
        free(elf_data);
        elf_loader_unload(handle);
        return NULL;
    }

    free(elf_data);
    ESP_LOGI(TAG, "ELF loaded: %s", handle->name);
    return handle;
}

void elf_loader_unload(elf_handle_t *handle) {
    if (!handle) return;

    if (handle->inst_mmap_handle) {
        esp_partition_munmap(handle->inst_mmap_handle);
    }
    if (handle->data_mmap_handle) {
        esp_partition_munmap(handle->data_mmap_handle);
    }
    if (handle->data_base) {
        free(handle->data_base);
    }
    if (handle->bss_base) {
        free(handle->bss_base);
    }
    free(handle);
}

void *elf_loader_symbol(elf_handle_t *handle, const char *name) {
    if (!handle || !name) return NULL;
    if (strcmp(name, "app_init") == 0) return (void *)handle->init;
    if (strcmp(name, "app_checkpoint") == 0) return (void *)handle->checkpoint;
    if (strcmp(name, "app_close") == 0) return (void *)handle->close;
    if (strcmp(name, "app_event") == 0) return (void *)handle->event_fn;
    return NULL;
}
