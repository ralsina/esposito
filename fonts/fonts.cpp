#include "fonts.h"
#include <lgfx/v1/lgfx_fonts.hpp>
#include <lgfx/utility/pgmspace.h>
#include "5x7.h"
#include "spleen-5x8.h"
#include "6x10.h"
#include "6x12.h"
#include "7x13.h"
#include "8x13.h"
#include "hardware.h"
#include "hack_font.h"
#include <cstdio>
#include <cstdlib>

font_info_t font_table[FONT_COUNT] = {
    { FONT_5X7,        "5x7",        5, 7, &FixedMono5x7Bitmaps },
    { FONT_SPLEEN_5X8, "spleen-5x8", 5, 8, &spleen_5x8 },
    { FONT_TOMTHUMB,   "tomthumb",   4, 6, &fonts::TomThumb },
    { FONT_6X10,       "6x10",       6, 10, &font_6x10 },
    { FONT_6X12,       "6x12",       6, 12, &font_6x12 },
    { FONT_7X13,       "7x13",       7, 13, &font_7x13 },
    { FONT_8X13,       "8x13",       8, 13, &font_8x13 },
    { FONT_HACK_VLW,   "hack",       0, 0, nullptr },  // metrics filled by vlw_init_embedded()
};

font_id_t font_lookup_by_name(const char *name) {
    for (int i = 0; i < FONT_COUNT; i++) {
        const char *fn = font_table[i].name;
        int j = 0;
        while (fn[j] && name[j]) {
            char a = fn[j], b = name[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            j++;
        }
        if (fn[j] == '\0' && name[j] == '\0') {
            return (font_id_t)i;
        }
    }
    return FONT_SPLEEN_5X8;
}

// Load VLW font from SD card
bool font_load_vlw(const char *path) {
    return display_load_vlw_font(path);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void vlw_get_metrics(const uint8_t *data, size_t size, int *width, int *height) {
    *width = 6;   // Safe defaults
    *height = 9;  // Safe defaults

    if (size < 24) {
        printf("VLW data too small: %zu bytes\n", size);
        return;
    }

    int glyph_count = (int)read_be32(data);
    if (glyph_count <= 0 || glyph_count > 1000) {
        printf("Invalid glyph count: %d\n", glyph_count);
        return;
    }

    int ascent   = abs((int32_t)read_be32(data + 16));
    int descent  = abs((int32_t)read_be32(data + 20));
    int yAdvance = (int)read_be32(data + 8);

    if (yAdvance <= 0) yAdvance = ascent + descent;
    if (yAdvance <= 0) yAdvance = 9;  // Fallback

    *height = yAdvance > ascent + descent ? yAdvance : ascent + descent;
    if (*height <= 0) *height = 9;  // Ensure positive height

    int max_advance = 0;
    for (int i = 0; i < glyph_count; i++) {
        int off = 24 + i * 28;
        if (off + 16 > (int)size) break;
        int xa = (int)read_be32(data + off + 12);
        if (xa > max_advance && xa < 100) max_advance = xa;  // Sanity check xa < 100
    }

    *width = max_advance > 0 ? max_advance : 6;
    if (*width <= 0) *width = 6;  // Ensure positive width

    printf("VLW metrics: glyph_count=%d, width=%d, height=%d\n", glyph_count, *width, *height);
}

// Initialize font_table metrics from embedded VLW data
void vlw_init_embedded_metrics(void) {
    // Set safe defaults first
    font_table[FONT_HACK_VLW].char_width = 6;
    font_table[FONT_HACK_VLW].char_height = 9;

    int w, h;
    vlw_get_metrics(hack_font, hack_font_size, &w, &h);

    // Only update if we got valid metrics
    if (w > 0 && h > 0) {
        font_table[FONT_HACK_VLW].char_width = w;
        font_table[FONT_HACK_VLW].char_height = h;
        printf("VLW font metrics updated: %dx%d\n", w, h);
    } else {
        printf("VLW font metrics invalid, using defaults: 6x9\n");
    }
}

// Load embedded VLW font from header data
bool font_load_embedded_vlw(void) {
    vlw_init_embedded_metrics();
    printf("Loading embedded VLW font (%zu bytes)\n", hack_font_size);
    return display_load_embedded_vlw_font(hack_font, hack_font_size);
}
