/*
 * Stub ESP-IDF surface for host-side compilation of elf_loader.c.
 *
 * The real headers live in ESP-IDF (components/esp_partition, esp_log, etc.)
 * and pull in FreeRTOS, the ROM, and hardware. This single header provides
 * just enough of the API surface that elf_loader.c compiles and links under
 * plain gcc, backed by the flash mock in elf_host_stubs.c.
 *
 * Include this BEFORE the real sources. It must not be used in firmware.
 */
#ifndef ELF_HOST_STUBS_H
#define ELF_HOST_STUBS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---- esp_err_t ---- */
typedef int esp_err_t;
#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_NO_MEM  0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x104

static inline const char *esp_err_to_name(esp_err_t err) {
    (void)err;
    return "ESP_ERR (stubbed)";
}

/* ---- esp_partition ---- */
#define ESP_PARTITION_TYPE_DATA              0x01
#define ESP_PARTITION_SUBTYPE_DATA_UNDEFINED 0x00

/* On hardware these distinguish instruction vs data cache mapping. The flash
 * mock returns a pointer into the backing RAM at the requested offset for
 * both, so the distinction is honored but has no runtime effect on host. */
typedef enum {
    ESP_PARTITION_MMAP_DATA,
    ESP_PARTITION_MMAP_INST,
} esp_partition_mmap_memory_t;

typedef int esp_partition_mmap_handle_t;

typedef struct esp_partition_t {
    uint32_t address;
    uint32_t size;
    const char *label;
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(uint32_t type,
                                                uint32_t subtype,
                                                const char *label);
esp_err_t esp_partition_erase_range(const esp_partition_t *part,
                                    uint32_t offset, uint32_t size);
esp_err_t esp_partition_write(const esp_partition_t *part, uint32_t dst_offset,
                              const void *src, uint32_t size);
esp_err_t esp_partition_mmap(const esp_partition_t *part, uint32_t offset,
                             uint32_t size, esp_partition_mmap_memory_t memory,
                             const void **out_ptr,
                             esp_partition_mmap_handle_t *out_handle);
void esp_partition_munmap(esp_partition_mmap_handle_t handle);

/* ---- esp_cache ---- */
#define ESP_CACHE_MSYNC_FLAG_INVALIDATE 0x01
static inline esp_err_t esp_cache_msync(const void *addr, uint32_t size,
                                        uint32_t flags) {
    (void)addr; (void)size; (void)flags;
    return ESP_OK;
}

/* ---- esp_log (as no-op macros) ---- */
#define ESP_LOGE(tag, fmt, ...)  do {} while (0)
#define ESP_LOGW(tag, fmt, ...)  do {} while (0)
#define ESP_LOGI(tag, fmt, ...)  do {} while (0)
#define ESP_LOGD(tag, fmt, ...)  do {} while (0)

/* ---- FreeRTOS (stub types only, loader doesn't call these) ---- */
/* freertos/FreeRTOS.h and freertos/task.h are included by elf_loader.c but
 * no FreeRTOS API is actually invoked, so empty headers suffice. */

#endif /* ELF_HOST_STUBS_H */
