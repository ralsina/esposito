#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FONT_VARIANT_REGULAR,
    FONT_VARIANT_BOLD,
    FONT_VARIANT_ITALIC,
    FONT_VARIANT_BOLDITALIC,
    FONT_VARIANT_COUNT
} font_variant_t;

typedef int font_id_t;

typedef struct {
    font_id_t id;
    const char *name;
    const char *family;
    int size;
    int char_width;
    int char_height;
} font_info_t;

// Embedded boot font (spleen-5x8.h, always available in PROGMEM)
#define FONT_SPLEEN 0

// Dynamic font registry — populated by font_cache_init()
extern font_info_t *font_table;
extern int font_count;

// Look up a font by its name string (e.g. "hack 10")
font_id_t font_lookup_by_name(const char *name);

// Parse VLW binary metrics (width, height) from raw VLW data
bool font_get_vlw_metrics(const uint8_t *data, size_t size, int *out_width, int *out_height);

// Initialize font cache: scan SD card for .fpack files and build font_table
// Must be called after sd_card_init(). Uses the fontcache flash partition.
bool font_cache_init(void);

// Load a font's .fpack from SD into the fontcache flash partition and mmap it
// After this, font_get_variant_data/font_get_supplement_data will return
// pointers into the mapped flash region for the given font.
bool font_cache_load(font_id_t id);

// Get the VLW data pointer for a specific variant of the currently active font
// Returns NULL if the font/variant is not available.
// The returned pointer is valid as long as font_cache_load is not called again.
const uint8_t *font_get_variant_data(font_id_t id, font_variant_t variant, size_t *out_size);

// Get supplement VLW data for the currently active font
const uint8_t *font_get_supplement_data(font_id_t id, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
