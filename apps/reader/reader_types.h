#ifndef READER_TYPES_H
#define READER_TYPES_H

#include <stdint.h>

#define MAX_RENDERED_LINES 40
#define LINE_BUF_MARGIN 32

#define MD_FORMAT_TOGGLE ((char)0x1D)
#define MD_FORMAT_BOLD ((char)0x1E)
#define MD_FORMAT_UNDERLINE ((char)0x1F)

typedef struct {
    char *text;
    uint8_t color;
    uint8_t attr;
} rendered_line_t;

#define NUM_RENDER_BUFFERS 3

typedef struct {
    rendered_line_t *lines;
    int line_count;
    long file_pos;
    long next_file_pos;
    int page_number;
} render_buffer_t;

#endif
