#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FONT_SPLEEN_5X8,
    FONT_TOMTHUMB,
    FONT_6X10,
    FONT_6X12,
    FONT_7X13,
    FONT_8X13,
    FONT_COUNT
} font_id_t;

typedef struct {
    font_id_t id;
    const char *name;
    int char_width;
    int char_height;
    const void *font_ptr;
} font_info_t;

extern const font_info_t font_table[FONT_COUNT];

font_id_t font_lookup_by_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif
