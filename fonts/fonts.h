#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FONT_5X7,
    FONT_SPLEEN_5X8,
    FONT_TOMTHUMB,
    FONT_6X10,
    FONT_6X12,
    FONT_7X13,
    FONT_8X13,
    FONT_HACK_VLW,  // 8px monospaced from SD card
    FONT_COUNT
} font_id_t;

typedef struct {
    font_id_t id;
    const char *name;
    int char_width;
    int char_height;
    const void *font_ptr;
} font_info_t;

extern font_info_t font_table[FONT_COUNT];

font_id_t font_lookup_by_name(const char *name);

// Load VLW font from SD card
bool font_load_vlw(const char *path);

// Load embedded VLW font from header data
bool font_load_embedded_vlw(void);

// Extract metrics (width, height) from VLW binary data
// Assumes monospaced-ish font — scans all glyphs for max xAdvance
void vlw_get_metrics(const uint8_t *data, size_t size, int *width, int *height);

// Update font_table VLW entry from the embedded hack_font data
void vlw_init_embedded_metrics(void);

#ifdef __cplusplus
}
#endif

#endif
