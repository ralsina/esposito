#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

#include <stdbool.h>
#include <stdio.h>
#include "text_mode.h"

#define MAX_FILENAME 256
#define MAX_STATUS_MSG 80
#define MAX_LINES_PER_PAGE 1000
#define MAX_CLIPBOARD_SIZE (64 * 1024)

// Page cache entry (borrowed from reader)
typedef struct {
    long file_pos;
    int screen_width;
    int content_rows;
    int state;
} page_cache_entry_t;

typedef struct {
    page_cache_entry_t entries[16];
    int count;
    int current;
} page_cache_t;

// Editor state structure
typedef struct {
    // File management
    FILE *file;
    char filename[MAX_FILENAME];
    size_t file_size;

    // Paging system
    page_cache_t page_cache;
    int current_page;
    int total_pages;

    // Text content (current page only)
    char **lines;
    int line_count;
    int line_alloc;  // Allocated capacity
    int lines_per_page; // Lines loaded per page (for page estimation)

    // Editing state
    int cursor_row;  // Logical line number (0-based)
    int cursor_col;  // Character position (0-based)
    int scroll_offset;  // First visible line
    int col_offset;    // Horizontal scroll offset

    // Line wrapping
    bool word_wrap;
    int *wrap_offsets;      // Wrap positions for each line
    int *display_to_logical; // Map display line to logical line
    int display_line_count; // Total display lines

    // Selection system
    bool selection_active;
    int sel_start_row, sel_start_col;
    int sel_end_row, sel_end_col;

    // Clipboard
    char *clipboard_buffer;
    int clipboard_size;

    // Editor state
    int dirty;
    char statusmsg[MAX_STATUS_MSG];
    char mode; // 'v' view, 'i' insert

    // Text prompt mode
    bool prompt_mode;
    char prompt_buf[128];
    int prompt_len;
    void (*prompt_callback)(const char *input);

    // Display
    int screen_rows;
    int screen_cols;
    int content_rows; // Rows available for content (excluding menu/status)

    // Menu bar
    int menu_active; // 0=none, 1=file, 2=edit, 3=view, 4=search, 5=help
} editor_state_t;

// Page cache functions
void page_cache_init(page_cache_t *cache);
bool page_cache_is_valid(page_cache_t *cache, int screen_width, int content_rows);
long page_cache_current_pos(page_cache_t *cache);
bool page_cache_can_next(page_cache_t *cache);
long page_cache_next(page_cache_t *cache);
bool page_cache_can_prev(page_cache_t *cache);
long page_cache_prev(page_cache_t *cache);
bool page_cache_add(page_cache_t *cache, long file_pos, int screen_width, int content_rows);
long estimate_next_page_offset(FILE *file, long current_offset, int lines_per_page);
int calculate_total_pages(FILE *file, int lines_per_page);

#endif