#include "fonts.h"
#include <lgfx/utility/pgmspace.h>
#include <cstdlib>

// All 28 Hack VLW font variants
#include "hack-6.h"
#include "hack_bold-6.h"
#include "hack_italic-6.h"
#include "hack_bolditalic-6.h"
#include "hack-7.h"
#include "hack_bold-7.h"
#include "hack_italic-7.h"
#include "hack_bolditalic-7.h"
#include "hack-8.h"
#include "hack_bold-8.h"
#include "hack_italic-8.h"
#include "hack_bolditalic-8.h"
#include "hack-9.h"
#include "hack_bold-9.h"
#include "hack_italic-9.h"
#include "hack_bolditalic-9.h"
#include "hack-10.h"
#include "hack_bold-10.h"
#include "hack_italic-10.h"
#include "hack_bolditalic-10.h"
#include "hack-11.h"
#include "hack_bold-11.h"
#include "hack_italic-11.h"
#include "hack_bolditalic-11.h"

// All 24 IBM Plex Mono VLW font variants
#include "ibmplex-6.h"
#include "ibmplex_bold-6.h"
#include "ibmplex_italic-6.h"
#include "ibmplex_bolditalic-6.h"
#include "ibmplex-7.h"
#include "ibmplex_bold-7.h"
#include "ibmplex_italic-7.h"
#include "ibmplex_bolditalic-7.h"
#include "ibmplex-8.h"
#include "ibmplex_bold-8.h"
#include "ibmplex_italic-8.h"
#include "ibmplex_bolditalic-8.h"
#include "ibmplex-9.h"
#include "ibmplex_bold-9.h"
#include "ibmplex_italic-9.h"
#include "ibmplex_bolditalic-9.h"
#include "ibmplex-10.h"
#include "ibmplex_bold-10.h"
#include "ibmplex_italic-10.h"
#include "ibmplex_bolditalic-10.h"
#include "ibmplex-11.h"
#include "ibmplex_bold-11.h"
#include "ibmplex_italic-11.h"
#include "ibmplex_bolditalic-11.h"

typedef struct {
    const uint8_t *data;
    size_t size;
} variant_data_t;

#define VDATA(v) { v, sizeof(v) }

static const variant_data_t font_variants[FONT_COUNT][FONT_VARIANT_COUNT] = {
    { VDATA(hack_6), VDATA(hack_bold_6), VDATA(hack_italic_6), VDATA(hack_bolditalic_6) },
    { VDATA(hack_7), VDATA(hack_bold_7), VDATA(hack_italic_7), VDATA(hack_bolditalic_7) },
    { VDATA(hack_8), VDATA(hack_bold_8), VDATA(hack_italic_8), VDATA(hack_bolditalic_8) },
    { VDATA(hack_9), VDATA(hack_bold_9), VDATA(hack_italic_9), VDATA(hack_bolditalic_9) },
    { VDATA(hack_10), VDATA(hack_bold_10), VDATA(hack_italic_10), VDATA(hack_bolditalic_10) },
    { VDATA(hack_11), VDATA(hack_bold_11), VDATA(hack_italic_11), VDATA(hack_bolditalic_11) },
    { VDATA(ibmplex_6), VDATA(ibmplex_bold_6), VDATA(ibmplex_italic_6), VDATA(ibmplex_bolditalic_6) },
    { VDATA(ibmplex_7), VDATA(ibmplex_bold_7), VDATA(ibmplex_italic_7), VDATA(ibmplex_bolditalic_7) },
    { VDATA(ibmplex_8), VDATA(ibmplex_bold_8), VDATA(ibmplex_italic_8), VDATA(ibmplex_bolditalic_8) },
    { VDATA(ibmplex_9), VDATA(ibmplex_bold_9), VDATA(ibmplex_italic_9), VDATA(ibmplex_bolditalic_9) },
    { VDATA(ibmplex_10), VDATA(ibmplex_bold_10), VDATA(ibmplex_italic_10), VDATA(ibmplex_bolditalic_10) },
    { VDATA(ibmplex_11), VDATA(ibmplex_bold_11), VDATA(ibmplex_italic_11), VDATA(ibmplex_bolditalic_11) },
};

font_info_t font_table[FONT_COUNT] = {
    { FONT_HACK_6,    "hack 6",    "hack",    6,  4, 7  },
    { FONT_HACK_7,    "hack 7",    "hack",    7,  4, 8  },
    { FONT_HACK_8,    "hack 8",    "hack",    8,  5, 9  },
    { FONT_HACK_9,    "hack 9",    "hack",    9,  5, 11 },
    { FONT_HACK_10,   "hack 10",   "hack",    10, 6, 12 },
    { FONT_HACK_11,   "hack 11",   "hack",    11, 7, 13 },
    { FONT_IBMPLEX_6, "ibmplex 6", "ibmplex", 6,  4, 8  },
    { FONT_IBMPLEX_7, "ibmplex 7", "ibmplex", 7,  4, 9  },
    { FONT_IBMPLEX_8, "ibmplex 8", "ibmplex", 8,  5, 11 },
    { FONT_IBMPLEX_9, "ibmplex 9", "ibmplex", 9,  5, 12 },
    { FONT_IBMPLEX_10,"ibmplex 10","ibmplex", 10, 6, 13 },
    { FONT_IBMPLEX_11,"ibmplex 11","ibmplex", 11, 7, 15 },
};



font_id_t font_lookup_by_name(const char *name) {
    if (!name) return FONT_INVALID;
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
        if (fn[j] == '\0' && name[j] == '\0')
            return font_table[i].id;
    }
    return FONT_INVALID;
}

const uint8_t *font_get_variant_data(font_id_t id, font_variant_t variant, size_t *out_size) {
    if (id < 0 || id >= FONT_COUNT) return NULL;
    if (variant < 0 || variant >= FONT_VARIANT_COUNT) variant = FONT_VARIANT_REGULAR;
    if (out_size) *out_size = font_variants[id][variant].size;
    return font_variants[id][variant].data;
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

bool font_get_vlw_metrics(const uint8_t *data, size_t size, int *out_width, int *out_height) {
    if (!data || size < 24) return false;

    int glyph_count = (int)read_be32(data + 0);
    if (glyph_count <= 0 || glyph_count > 1000) return false;

    int ascent  = abs((int32_t)read_be32(data + 16));
    int descent = abs((int32_t)read_be32(data + 20));
    int yAdvance = (int)read_be32(data + 8);

    if (yAdvance <= 0) yAdvance = ascent + descent;
    int height = yAdvance > ascent + descent ? yAdvance : ascent + descent;
    if (height <= 0) height = 9;

    int max_advance = 0;
    for (int i = 0; i < glyph_count; i++) {
        int off = 24 + i * 28;
        if (off + 16 > (int)size) break;
        int xa = (int)read_be32(data + off + 12);
        if (xa > max_advance && xa < 100) max_advance = xa;
    }

    int width = max_advance > 0 ? max_advance : 6;
    if (width <= 0) width = 6;

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return true;
}
