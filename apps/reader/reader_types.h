#ifndef READER_TYPES_H
#define READER_TYPES_H

#include <stdint.h>

#define MAX_RENDERED_LINES 40
#define MAX_LINE_TEXT 128

#define MD_FORMAT_TOGGLE ((char)0x1D)
#define MD_FORMAT_BOLD ((char)0x1E)
#define MD_FORMAT_UNDERLINE ((char)0x1F)

typedef struct {
    char text[MAX_LINE_TEXT];
    uint8_t color;
    uint8_t attr;
} rendered_line_t;

#endif
