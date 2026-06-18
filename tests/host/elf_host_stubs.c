/*
 * Flash mock — backs the stubbed esp_partition API with a RAM buffer.
 *
 * Allocates a single fixed-size "partition" with mmap(MAP_32BIT) so the
 * returned addresses fit in 32 bits — elf_loader.c casts pointers to
 * uint32_t (it runs on a 32-bit MCU), and on a 64-bit host a heap address
 * would be truncated and corrupt every relocation.
 *
 * The loader's own flow works naturally on top of this: it erases the whole
 * partition, then writes sections to it, then mmaps regions of it. Since the
 * mmap just returns (buffer + offset), writes through esp_partition_write are
 * immediately visible through the mmap'd pointer — same as real flash after a
 * cache invalidate.
 */
#include "elf_host_stubs.h"
#include "os_symtab.h"
#include <sys/mman.h>
#include <stdlib.h>

#define MOCK_PARTITION_SIZE (512 * 1024)  /* 512 KB, ample for test fixtures */
#define APP_PARTITION_LABEL "app_code"

static uint8_t *g_flash = NULL;
static esp_partition_t g_partition;

/* Lazily set up the RAM-backed partition on first use. */
static void ensure_flash(void) {
    if (g_flash) return;
    void *p = mmap(NULL, MOCK_PARTITION_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p == MAP_FAILED) {
        /* Fall back to a plain malloc if the kernel rejects MAP_32BIT
         * (non-x86-64 hosts). Relocation correctness tests still hold; only
         * the address-fits-in-uint32 assumption is weakened. */
        p = calloc(1, MOCK_PARTITION_SIZE);
        if (!p) abort();
    }
    g_flash = (uint8_t *)p;
    memset(g_flash, 0xFF, MOCK_PARTITION_SIZE);  /* erased flash reads 0xFF */
    g_partition.address = 0;
    g_partition.size = MOCK_PARTITION_SIZE;
    g_partition.label = APP_PARTITION_LABEL;
}

/* Reset between tests: zero + re-erase so a prior run can't leak state. */
void flash_mock_reset(void) {
    if (!g_flash) { ensure_flash(); return; }
    memset(g_flash, 0xFF, MOCK_PARTITION_SIZE);
}

const esp_partition_t *esp_partition_find_first(uint32_t type,
                                                uint32_t subtype,
                                                const char *label) {
    (void)type; (void)subtype;
    ensure_flash();
    if (label && strcmp(label, APP_PARTITION_LABEL) != 0) return NULL;
    return &g_partition;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *part,
                                    uint32_t offset, uint32_t size) {
    if (!part) return ESP_ERR_INVALID_ARG;
    if (offset + size > part->size) return ESP_ERR_INVALID_ARG;
    ensure_flash();
    memset(g_flash + offset, 0xFF, size);
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *part, uint32_t dst_offset,
                              const void *src, uint32_t size) {
    if (!part || !src) return ESP_ERR_INVALID_ARG;
    if (dst_offset + size > part->size) return ESP_ERR_INVALID_ARG;
    ensure_flash();
    memmove(g_flash + dst_offset, src, size);
    return ESP_OK;
}

esp_err_t esp_partition_mmap(const esp_partition_t *part, uint32_t offset,
                             uint32_t size, esp_partition_mmap_memory_t memory,
                             const void **out_ptr,
                             esp_partition_mmap_handle_t *out_handle) {
    (void)memory;
    if (!part || !out_ptr || !out_handle) return ESP_ERR_INVALID_ARG;
    if (offset + size > part->size) return ESP_ERR_INVALID_ARG;
    ensure_flash();
    *out_ptr = g_flash + offset;
    *out_handle = (esp_partition_mmap_handle_t)(offset + 1);  /* non-zero sentinel */
    return ESP_OK;
}

void esp_partition_munmap(esp_partition_mmap_handle_t handle) {
    (void)handle;  /* no-op: RAM-backed, no mapping to tear down */
}

/* ---- app_heap: redirect to the host allocator ---- */
void *app_malloc(size_t size)            { return malloc(size); }
void *app_calloc(size_t count, size_t sz){ return calloc(count, sz); }
void *app_realloc(void *ptr, size_t sz)  { return realloc(ptr, sz); }
void  app_free(void *ptr)                { free(ptr); }

/* app_heap lifecycle: the loader doesn't call init/reset/release, but link
 * them anyway in case future code does. */
bool  app_heap_init(void)                { return true; }
void  app_heap_reset(void)               {}
void  app_heap_release(void)             {}
void  app_heap_log_stats(const char *l)  { (void)l; }

/* ---- os_symtab: expose a couple of host test symbols for relocation ----
 *
 * The loader resolves undefined symbols in the app's symtab against this
 * table via os_symtab_lookup. To exercise relocation end-to-end we expose
 * two dummy exported functions whose addresses get patched into .text. */
static int g_host_export_a = 0xDEADBEEF;
static int g_host_export_b = 0xCAFEBABE;

static const os_symtab_entry_t g_host_syms[] = {
    { "host_export_a", (void *)&g_host_export_a },
    { "host_export_b", (void *)&g_host_export_b },
    { NULL, NULL },
};

const os_symtab_entry_t *os_symtab_lookup(const char *name) {
    if (!name) return NULL;
    for (int i = 0; g_host_syms[i].name; i++) {
        if (strcmp(g_host_syms[i].name, name) == 0) return &g_host_syms[i];
    }
    return NULL;
}

int os_symtab_count(void) {
    return (int)(sizeof(g_host_syms) / sizeof(g_host_syms[0]) - 1);
}

const os_symtab_entry_t *os_symtab_get(int index) {
    if (index < 0 || index >= os_symtab_count()) return NULL;
    return &g_host_syms[index];
}
