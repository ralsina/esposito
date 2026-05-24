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

// Get the file position when the page was actually full
// This is different from the final file position, which may be later
// if md_scan_page continued reading to finish a paragraph/element
long md_get_page_full_position(void);

// Parser state enumeration for tracking element continuation
typedef enum {
    MD_STATE_DEFAULT,         // Default state = paragraph processing
    MD_STATE_HEADING_1,       // In the middle of a level 1 heading
    MD_STATE_HEADING_2,       // In the middle of a level 2 heading
    MD_STATE_HEADING_3,       // In the middle of a level 3 heading
    MD_STATE_HEADING_4,       // In the middle of a level 4 heading
    MD_STATE_HEADING_5,       // In the middle of a level 5 heading
    MD_STATE_HEADING_6,       // In the middle of a level 6 heading
} md_parser_state_enum_t;

// Minimal parser state for deterministic page caching
// Instead of storing remainder content, we store the parsing CONTEXT
// The parser will continue reading from the file position in this context
typedef struct {
    md_parser_state_enum_t state;  // Current parsing state
    int carry_spacer;               // Spacer state between elements
    int in_tag;                     // Tag processing state
} md_parser_state_t;

void md_get_parser_state(md_parser_state_t *state);
void md_set_parser_state(const md_parser_state_t *state);

#endif
