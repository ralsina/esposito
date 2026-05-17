#include "fonts.h"
#include <lgfx/v1/lgfx_fonts.hpp>
#include <lgfx/utility/pgmspace.h>
#include "spleen-5x8.h"
#include "6x10.h"
#include "6x12.h"
#include "7x13.h"
#include "8x13.h"

const font_info_t font_table[FONT_COUNT] = {
    { FONT_SPLEEN_5X8, "spleen-5x8", 5, 8, &spleen_5x8 },
    { FONT_TOMTHUMB,   "tomthumb",   4, 6, &fonts::TomThumb },
    { FONT_6X10,       "6x10",       6, 10, &font_6x10 },
    { FONT_6X12,       "6x12",       6, 12, &font_6x12 },
    { FONT_7X13,       "7x13",       7, 13, &font_7x13 },
    { FONT_8X13,       "8x13",       8, 13, &font_8x13 },
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
