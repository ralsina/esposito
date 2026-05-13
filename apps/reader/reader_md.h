#ifndef READER_MD_H
#define READER_MD_H

#include <stdint.h>
#include <stdio.h>

#define MAX_RENDERED_LINES 40
#define MAX_LINE_TEXT 128

// Inline formatting control marker used inside rendered_line_t.text.
// The renderer treats it as a zero-width toggle for underline.
#define MD_LINK_TOGGLE ((char)0x1D)

typedef struct {
    char text[MAX_LINE_TEXT];
    uint8_t color;
    uint8_t attr;
} rendered_line_t;

int md_scan_page(FILE *f, rendered_line_t *lines, int max_lines, int screen_width);
void md_clear_remainder(void);

#endif
