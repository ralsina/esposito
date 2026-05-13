#include "fonts.h"
#include <lgfx/v1/lgfx_fonts.hpp>
#include <lgfx/utility/pgmspace.h>
#include "spleen-5x8.h"

const font_info_t font_table[FONT_COUNT] = {
    { FONT_SPLEEN_5X8, "spleen-5x8", 5, 8, &spleen_5x8 },
    { FONT_TOMTHUMB,   "tomthumb",   4, 6, &fonts::TomThumb },
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
