#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_HACK_6   0
#define FONT_HACK_7   1
#define FONT_HACK_8   2
#define FONT_HACK_9   3
#define FONT_HACK_10  4
#define FONT_HACK_11  5
#define FONT_HACK_12  6
#define FONT_HACK_13  7
#define FONT_HACK_14  8
#define FONT_IBMPLEX_6  9
#define FONT_IBMPLEX_7  10
#define FONT_IBMPLEX_8  11
#define FONT_IBMPLEX_9  12
#define FONT_IBMPLEX_10 13
#define FONT_IBMPLEX_11 14
#define FONT_IBMPLEX_12 15
#define FONT_IBMPLEX_13 16
#define FONT_IBMPLEX_14 17
#define FONT_COUNT      18
#define FONT_INVALID    (-1)

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

extern font_info_t font_table[];

font_id_t font_lookup_by_name(const char *name);

bool font_get_vlw_metrics(const uint8_t *data, size_t size, int *out_width, int *out_height);

const uint8_t *font_get_variant_data(font_id_t id, font_variant_t variant, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
