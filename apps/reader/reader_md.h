#ifndef READER_MD_H
#define READER_MD_H

#include <stdint.h>
#include <stdio.h>

#define MAX_RENDERED_LINES 40
#define MAX_LINE_TEXT 128

// Inline formatting control marker used inside rendered_line_t.text.
// The renderer treats it as a zero-width toggle for underline.
#define MD_FORMAT_TOGGLE ((char)0x1D)

typedef struct {
    char text[MAX_LINE_TEXT];
    uint8_t color;
    uint8_t attr;
} rendered_line_t;

int md_scan_page_with_levels(FILE *f, rendered_line_t *lines, uint8_t *heading_levels, int max_lines, int screen_width);
int md_scan_page(FILE *f, rendered_line_t *lines, int max_lines, int screen_width);
void md_clear_remainder(void);

// Minimal parser state for deterministic page caching
// Instead of storing remainder content, we store the parsing CONTEXT
// The parser will continue reading from the file position in this context
typedef struct {
    int in_paragraph;         // Currently processing a paragraph (continue it)
    int in_heading;           // Currently processing a heading (continue it)
    int heading_level;        // If in heading, what level
    int carry_spacer;         // Spacer state between elements
    int in_tag;               // Tag processing state
} md_parser_state_t;

void md_get_parser_state(md_parser_state_t *state);
void md_set_parser_state(const md_parser_state_t *state);

#endif
