#ifndef READER_RENDERER_H
#define READER_RENDERER_H

#include "reader_token.h"
#include "reader_types.h"
#include <stdio.h>
#include <stdbool.h>

// Renderer states (correspond to parser states)
typedef enum {
    RENDER_STATE_DEFAULT,      // Paragraph mode
    RENDER_STATE_HEADING_1,
    RENDER_STATE_HEADING_2,
    RENDER_STATE_HEADING_3,
    RENDER_STATE_HEADING_4,
    RENDER_STATE_HEADING_5,
    RENDER_STATE_HEADING_6,
} render_state_t;

// Page cache entry - stores position and state for each page
typedef struct {
    long file_pos;              // File position to resume from
    render_state_t state;       // State to resume in
    int screen_width;            // Screen dimensions when cached
    int content_rows;
} page_cache_entry_t;

// Page cache
#define PAGE_CACHE_ENTRIES 16

typedef struct {
    page_cache_entry_t entries[16];
    int count;
    int current;
} page_cache_t;

// Page renderer state
typedef struct {
    tokenizer_t tokenizer;
    render_state_t state;
    int current_line;
    int max_lines;
    int screen_width;
    rendered_line_t *lines;
    uint8_t *heading_levels;
    int line_count;
    bool in_paragraph;          // Currently building a paragraph
    bool page_full;             // Page has been filled
    bool needs_blank_line;      // Paragraph break pending, skip next blank line
} page_renderer_t;

// Initialize renderer
void renderer_init(page_renderer_t *renderer, FILE *file, rendered_line_t *lines, uint8_t *heading_levels, int max_lines, int screen_width);

// Process tokens until page is full or EOF
bool renderer_process_page(page_renderer_t *renderer);

// Get current file position (for caching)
long renderer_get_position(page_renderer_t *renderer);

// Get current state (for caching)
render_state_t renderer_get_state(page_renderer_t *renderer);

// Set position and state (for page navigation)
void renderer_set_position(page_renderer_t *renderer, long file_pos, render_state_t state, int screen_width, int content_rows);

// Page cache functions
void page_cache_init(page_cache_t *cache);
bool page_cache_add(page_cache_t *cache, long file_pos, render_state_t state, int screen_width, int content_rows);
bool page_cache_can_prev(page_cache_t *cache);
bool page_cache_can_next(page_cache_t *cache);
long page_cache_prev(page_cache_t *cache);
long page_cache_next(page_cache_t *cache);
page_cache_entry_t* page_cache_current(page_cache_t *cache);
bool page_cache_is_valid(page_cache_t *cache, int screen_width, int content_rows);

#endif