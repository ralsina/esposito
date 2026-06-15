/* Hecto - Paged Text Editor for Esposito
 * Based on kilo editor and reader app architecture
 */

#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"
#include "editor_state.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "hecto";
static editor_state_t *state = NULL;

// Key code definitions (from BBQ20 keyboard)
#define KEY_F1          0x81
#define KEY_F2          0x82
#define KEY_F3          0x83
#define KEY_F10         0x8A
#define KEY_UP          0x99
#define KEY_DOWN        0x98
#define KEY_LEFT        0x97
#define KEY_RIGHT       0x96
#define KEY_HOME        0x91
#define KEY_END         0x94
#define KEY_PGUP        0x92
#define KEY_PGDN        0x95
#define KEY_DELETE      0x93
#define KEY_ENTER       '\n'
#define KEY_BACKSPACE   '\b'

// Forward declarations
static void init_state(void);
static void cleanup_state(void);
static void render_menu_bar(void);
static void render_status_bar(void);
static void render_content(void);
static void handle_keyboard_event(app_context_t *ctx, event_t *event);
static void handle_touch_event(event_t *event);
static void load_page_at_offset(long offset);
static void save_current_page(void);
static void open_file(const char *path);
static void new_file(void);
static void save_file(void);
static void save_file_as(void);
static void navigate_next_page(void);
static void navigate_prev_page(void);
static void navigate_first_page(void);
static void navigate_last_page(void);
void app_checkpoint(app_context_t *ctx);
static void selection_clear(void);

// Initialize editor state
static void init_state(void) {
    state = malloc(sizeof(editor_state_t));
    if (!state) {
        os_log(TAG, "Failed to allocate state");
        return;
    }

    memset(state, 0, sizeof(editor_state_t));

    // Get screen dimensions
    state->screen_cols = text_mode_get_cols();
    state->screen_rows = text_mode_get_rows();

    // Validate screen dimensions
    if (state->screen_cols <= 0 || state->screen_rows <= 0) {
        os_log(TAG, "Invalid screen dimensions: %dx%d", state->screen_cols, state->screen_rows);
        // Set defaults
        state->screen_cols = 64;
        state->screen_rows = 30;
    }

    state->content_rows = state->screen_rows - 2; // Menu bar + status bar

    os_log(TAG, "Screen: %dx%d, Content: %d rows",
           state->screen_cols, state->screen_rows, state->content_rows);

    // Initialize page cache
    page_cache_init(&state->page_cache);

    // Allocate line buffer (larger pages for smoother scrolling)
    state->lines_per_page = 100;
    state->line_alloc = state->lines_per_page + 10;
    state->lines = malloc(sizeof(char*) * state->line_alloc);
    if (state->lines) {
        memset(state->lines, 0, sizeof(char*) * state->line_alloc);
    }

    // Allocate wrap offsets
    state->wrap_offsets = malloc(sizeof(int) * state->line_alloc);
    state->display_to_logical = malloc(sizeof(int) * state->line_alloc);
    if (state->wrap_offsets && state->display_to_logical) {
        memset(state->wrap_offsets, 0, sizeof(int) * state->line_alloc);
        memset(state->display_to_logical, 0, sizeof(int) * state->line_alloc);
    }

    // Allocate clipboard
    state->clipboard_buffer = malloc(MAX_CLIPBOARD_SIZE);
    if (state->clipboard_buffer) {
        state->clipboard_buffer[0] = '\0';
    }

    // Set initial mode
    state->mode = 'i';
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;
    state->col_offset = 0;

    // Start with one empty line
    state->lines[0] = malloc(1);
    if (state->lines[0]) {
        state->lines[0][0] = '\0';
        state->line_count = 1;
    }

    strcpy(state->statusmsg, "Hecto - Ctrl+S=Save Ctrl+N=New Fn+WASD=Move Ctrl+Q=Quit");

    os_log(TAG, "State initialized: %dx%d screen", state->screen_cols, state->screen_rows);
}

// Cleanup editor state
static void cleanup_state(void) {
    if (!state) return;

    // Close file
    if (state->file) {
        fclose(state->file);
        state->file = NULL;
    }

    // Free lines
    if (state->lines) {
        for (int i = 0; i < state->line_count; i++) {
            if (state->lines[i]) {
                free(state->lines[i]);
            }
        }
        free(state->lines);
        state->lines = NULL;
    }

    // Free wrap offsets
    if (state->wrap_offsets) {
        free(state->wrap_offsets);
        state->wrap_offsets = NULL;
    }

    if (state->display_to_logical) {
        free(state->display_to_logical);
        state->display_to_logical = NULL;
    }

    // Free clipboard
    if (state->clipboard_buffer) {
        free(state->clipboard_buffer);
        state->clipboard_buffer = NULL;
    }

    // Free state
    free(state);
    state = NULL;
}

// Render menu bar
static void render_menu_bar(void) {
    // Fill the entire menu bar row with spaces on white background
    for (int x = 0; x < state->screen_cols; x++) {
        text_mode_print_at_attr_bg(x, 0, " ", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    }

    // Print menu items with black text on white background
    text_mode_print_at_attr_bg(1, 0, "Open", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(8, 0, "Save", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(15, 0, "Help", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
}

// Render status bar
static void render_status_bar(void) {
    int y = state->screen_rows - 1;

    // Clear status bar
    for (int x = 0; x < state->screen_cols; x++) {
        text_mode_print_at(x, y, " ");
    }

    if (state->prompt_mode) {
        text_mode_print_at_attr_bg(0, y, state->statusmsg, TEXT_COLOR_BRIGHT_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);
        text_mode_print_at(strlen(state->statusmsg), y, state->prompt_buf);
        text_mode_set_cursor(strlen(state->statusmsg) + state->prompt_len, y);
        return;
    }

    // Position info
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "POS: %d,%d", state->cursor_row + 1, state->cursor_col + 1);
    text_mode_print_at(0, y, pos_buf);

    // Page info
    char page_buf[32];
    snprintf(page_buf, sizeof(page_buf), "PAGE: %d/%d",
             state->current_page + 1, state->total_pages > 0 ? state->total_pages : 1);
    text_mode_print_at(15, y, page_buf);

    // Filename with dirty indicator
    char file_buf[64];
    const char *basename = strrchr(state->filename, '/');
    if (!basename && state->filename[0]) basename = state->filename;
    else if (basename) basename++;

    snprintf(file_buf, sizeof(file_buf), "%s%s",
             basename ? basename : "(unnamed)",
             state->dirty ? " [+]" : "");
    text_mode_print_at(30, y, file_buf);

    // Status message
    if (state->statusmsg[0]) {
        text_mode_print_at(state->screen_cols - strlen(state->statusmsg) - 1, y, state->statusmsg);
    }
}

// Render content area
static void render_content(void) {
    // Show message if no content
    if (state->line_count == 0 || (state->line_count == 1 && state->lines[0] && state->lines[0][0] == '\0')) {
        // Clear content area
        for (int y = 1; y < state->screen_rows - 1; y++) {
            for (int x = 0; x < state->screen_cols; x++) {
                text_mode_print_at(x, y, " ");
            }
        }
        text_mode_print_at(2, 2, "Hecto - Text Editor");
        text_mode_print_at(2, 4, "Ctrl+S = Save");
        text_mode_print_at(2, 5, "Ctrl+N = New file");
        text_mode_print_at(2, 6, "Ctrl+Q = Quit");
        text_mode_print_at(2, 8, "Fn+WASD = Move cursor");
        text_mode_print_at(2, 10, "Just start typing...");
        // Set cursor at typing position
        text_mode_set_cursor(2, 12);
        return;
    }

    // Normalize selection range for rendering
    int sel_start_row = 0, sel_start_col = 0, sel_end_row = 0, sel_end_col = 0;
    bool has_selection = state->selection_active;
    if (has_selection) {
        if (state->sel_start_row < state->sel_end_row ||
            (state->sel_start_row == state->sel_end_row && state->sel_start_col < state->sel_end_col)) {
            sel_start_row = state->sel_start_row;
            sel_start_col = state->sel_start_col;
            sel_end_row = state->sel_end_row;
            sel_end_col = state->sel_end_col;
        } else {
            sel_start_row = state->sel_end_row;
            sel_start_col = state->sel_end_col;
            sel_end_row = state->sel_start_row;
            sel_end_col = state->sel_start_col;
        }
    }

    // Render visible lines (no need to clear entire area, just update what changes)
    int display_y = 1;
    for (int i = state->scroll_offset; i < state->line_count && display_y < state->screen_rows - 1; i++) {
        if (state->lines[i]) {
            int len = strlen(state->lines[i]);

            // Calculate visible portion based on col_offset
            int start_col = state->col_offset;
            if (start_col >= len) {
                start_col = len; // Beyond line end
            }

            int visible_len = len - start_col;
            if (visible_len > state->screen_cols) {
                visible_len = state->screen_cols;
            }

            // Render the visible portion of the line, with selection highlighting
            if (visible_len > 0) {
                for (int x = 0; x < visible_len; x++) {
                    int col = start_col + x;
                    char ch[2] = {state->lines[i][col], '\0'};

                    // Check if this character is within the selection
                    bool selected = false;
                    if (has_selection && i >= sel_start_row && i <= sel_end_row) {
                        if (i == sel_start_row && i == sel_end_row) {
                            selected = (col >= sel_start_col && col < sel_end_col);
                        } else if (i == sel_start_row) {
                            selected = (col >= sel_start_col);
                        } else if (i == sel_end_row) {
                            selected = (col < sel_end_col);
                        } else {
                            selected = true;
                        }
                    }

                    if (selected) {
                        text_mode_print_at_attr_bg(x, display_y, ch,
                                                   TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
                    } else {
                        text_mode_print_at(x, display_y, ch);
                    }
                }
            }

            // Clear rest of line with spaces
            for (int x = visible_len; x < state->screen_cols; x++) {
                text_mode_print_at(x, display_y, " ");
            }
        } else {
            // Empty line - clear it
            for (int x = 0; x < state->screen_cols; x++) {
                text_mode_print_at(x, display_y, " ");
            }
        }
        display_y++;
    }

    // Set cursor position - make sure it's visible
    int cursor_y = 1 + (state->cursor_row - state->scroll_offset);

    // Validate cursor position
    if (cursor_y < 1) {
        cursor_y = 1;
        state->cursor_row = state->scroll_offset;
    }
    if (cursor_y >= state->screen_rows - 1) {
        cursor_y = state->screen_rows - 2;
        state->cursor_row = state->scroll_offset + (cursor_y - 1);
    }

    // Set the cursor and make it visible by drawing a highlighted block
    text_mode_set_cursor(state->cursor_col - state->col_offset, cursor_y);

    // Draw a visible cursor block (inverse color)
    char current_char = ' ';
    int cursor_x = state->cursor_col - state->col_offset;

    if (state->cursor_row < state->line_count && state->lines[state->cursor_row]) {
        int len = strlen(state->lines[state->cursor_row]);

        // If cursor is at or past the end of line, show it after the last character
        if (state->cursor_col >= len) {
            cursor_x = len - state->col_offset;  // Account for horizontal scroll
            if (cursor_x < 0) cursor_x = 0;  // Safety check
            current_char = ' ';  // Show space cursor at end
        } else {
            // Cursor is within the line content
            current_char = state->lines[state->cursor_row][state->cursor_col];
            cursor_x = state->cursor_col;
        }
    }

    // Draw cursor as inverted character
    text_mode_print_at_attr_bg(cursor_x, cursor_y, &current_char,
                               TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    os_log(TAG, "Cursor: row=%d col=%d (display x=%d, y=%d) char='%c' len=%d",
           state->cursor_row, state->cursor_col, cursor_x, cursor_y,
           current_char, state->lines[state->cursor_row] ? strlen(state->lines[state->cursor_row]) : 0);
}

// Load page at file offset
static void load_page_at_offset(long offset) {
    if (!state->file) return;

    os_log(TAG, "Loading page at offset %ld", offset);

    // Seek to offset
    if (fseek(state->file, offset, SEEK_SET) != 0) {
        os_log(TAG, "Failed to seek to offset %ld", offset);
        return;
    }

    // Clear existing lines
    for (int i = 0; i < state->line_count; i++) {
        if (state->lines[i]) {
            free(state->lines[i]);
            state->lines[i] = NULL;
        }
    }
    state->line_count = 0;

    // Read lines up to allocation limit
    char buffer[512];
    int lines_to_read = state->line_alloc;

    while (fgets(buffer, sizeof(buffer), state->file) && state->line_count < lines_to_read) {
        // Remove newline
        int len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        }
        if (len > 0 && buffer[len - 1] == '\r') {
            buffer[len - 1] = '\0';
            len--;
        }

        // Allocate line
        state->lines[state->line_count] = malloc(len + 1);
        if (state->lines[state->line_count]) {
            strcpy(state->lines[state->line_count], buffer);
            state->line_count++;
        } else {
            os_log(TAG, "Failed to allocate line buffer");
            break;
        }
    }

    // Update page cache entry
    if (state->page_cache.current >= 0 && state->page_cache.current < 16) {
        state->page_cache.entries[state->page_cache.current].file_pos = offset;
        state->page_cache.entries[state->page_cache.current].screen_width = state->screen_cols;
        state->page_cache.entries[state->page_cache.current].content_rows = state->content_rows;
    }

    os_log(TAG, "Loaded page: %d lines at offset %ld", state->line_count, offset);
}

// Open file
static void open_file(const char *path) {
    // Close existing file
    if (state->file) {
        fclose(state->file);
        state->file = NULL;
    }

    // Copy filename
    strncpy(state->filename, path, sizeof(state->filename) - 1);
    state->filename[sizeof(state->filename) - 1] = '\0';

    // Open file
    state->file = fopen(path, "r");
    if (!state->file) {
        os_log(TAG, "Failed to open file: %s", path);
        snprintf(state->statusmsg, sizeof(state->statusmsg), "Failed to open: %s", path);
        return;
    }

    // Get file size
    fseek(state->file, 0, SEEK_END);
    state->file_size = ftell(state->file);
    fseek(state->file, 0, SEEK_SET);

    // Calculate total pages accurately
    state->total_pages = calculate_total_pages(state->file, state->lines_per_page);
    state->current_page = 0;

    // Initialize page cache
    page_cache_init(&state->page_cache);
    page_cache_add(&state->page_cache, 0, state->screen_cols, state->content_rows);

    // Load first page
    load_page_at_offset(0);

    // Preload next page if available
    if (state->total_pages > 1) {
        long next_offset = estimate_next_page_offset(state->file, 0, state->lines_per_page);
        if (next_offset > 0) {
            page_cache_add(&state->page_cache, next_offset, state->screen_cols, state->content_rows);
        }
    }

    // Reset cursor and selection
    selection_clear();
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;

    snprintf(state->statusmsg, sizeof(state->statusmsg), "Opened: %s (%lu bytes, %d pages)",
             strrchr(path, '/') ? strrchr(path, '/') + 1 : path, state->file_size, state->total_pages);
}

// New file
static void new_file(void) {
    // Close existing file
    if (state->file) {
        fclose(state->file);
        state->file = NULL;
    }

    // Clear state
    state->filename[0] = '\0';
    state->file_size = 0;
    state->current_page = 0;
    state->total_pages = 1;
    state->dirty = 0;

    // Clear lines
    for (int i = 0; i < state->line_count; i++) {
        if (state->lines[i]) {
            free(state->lines[i]);
            state->lines[i] = NULL;
        }
    }
    state->line_count = 0;

    // Add one empty line
    state->lines[0] = malloc(1);
    if (state->lines[0]) {
        state->lines[0][0] = '\0';
        state->line_count = 1;
    }

    // Reset cursor and selection
    selection_clear();
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;

    strcpy(state->statusmsg, "New file");
}

// Save file
static void save_file(void) {
    if (!state->filename[0]) {
        save_file_as();
        return;
    }

    // Open file for writing
    FILE *f = fopen(state->filename, "w");
    if (!f) {
        os_log(TAG, "Failed to open file for writing: %s", state->filename);
        snprintf(state->statusmsg, sizeof(state->statusmsg), "Failed to save: %s", state->filename);
        return;
    }

    // Write lines
    for (int i = 0; i < state->line_count; i++) {
        if (state->lines[i]) {
            fwrite(state->lines[i], 1, strlen(state->lines[i]), f);
            fwrite("\n", 1, 1, f);
        } else {
            fwrite("\n", 1, 1, f);
        }
    }

    fclose(f);
    state->dirty = 0;

    snprintf(state->statusmsg, sizeof(state->statusmsg), "Saved: %s",
             strrchr(state->filename, '/') ? strrchr(state->filename, '/') + 1 : state->filename);
}

static void save_file_as_callback(const char *filename) {
    if (!filename || !filename[0]) {
        strcpy(state->statusmsg, "Invalid filename");
        return;
    }
    strncpy(state->filename, filename, sizeof(state->filename) - 1);
    state->filename[sizeof(state->filename) - 1] = '\0';
    save_file();
}

// Save file as
static void save_file_as(void) {
    state->prompt_mode = true;
    state->prompt_len = 0;
    state->prompt_buf[0] = '\0';
    state->prompt_callback = save_file_as_callback;
    strcpy(state->statusmsg, "Save file as: ");
}

// Navigate to next page
static void navigate_next_page(void) {
    if (!state->file || state->current_page >= state->total_pages - 1) {
        strcpy(state->statusmsg, "Already at last page");
        return;
    }

    selection_clear();

    // Check if next page is in cache
    if (page_cache_can_next(&state->page_cache)) {
        page_cache_next(&state->page_cache);
        long offset = page_cache_current_pos(&state->page_cache);
        load_page_at_offset(offset);
        state->current_page++;
    } else {
        // Need to load new page - estimate offset
        long current_offset = page_cache_current_pos(&state->page_cache);
        long next_offset = estimate_next_page_offset(state->file, current_offset, state->lines_per_page);

        if (next_offset > current_offset) {
            // Add to cache
            if (page_cache_add(&state->page_cache, next_offset, state->screen_cols, state->content_rows)) {
                page_cache_next(&state->page_cache);
                load_page_at_offset(next_offset);
                state->current_page++;

                // Preload next page if available
                if (state->current_page < state->total_pages - 1) {
                    long future_offset = estimate_next_page_offset(state->file, next_offset, state->lines_per_page);
                    if (future_offset > next_offset) {
                        page_cache_add(&state->page_cache, future_offset, state->screen_cols, state->content_rows);
                    }
                }
            }
        }
    }

    // Reset cursor position
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;

    snprintf(state->statusmsg, sizeof(state->statusmsg), "Page %d/%d", state->current_page + 1, state->total_pages);
}

// Navigate to previous page
static void navigate_prev_page(void) {
    if (!state->file || state->current_page <= 0) {
        strcpy(state->statusmsg, "Already at first page");
        return;
    }

    selection_clear();

    // Check if previous page is in cache
    if (page_cache_can_prev(&state->page_cache)) {
        page_cache_prev(&state->page_cache);
        long offset = page_cache_current_pos(&state->page_cache);
        load_page_at_offset(offset);
        state->current_page--;
    } else {
        // Previous page not in cache - need to reload from file
        // For now, just show message
        strcpy(state->statusmsg, "Previous page not cached");
        return;
    }

    // Reset cursor position
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;

    snprintf(state->statusmsg, sizeof(state->statusmsg), "Page %d/%d", state->current_page + 1, state->total_pages);
}

// Navigate to first page
static void navigate_first_page(void) {
    if (!state->file || state->current_page == 0) {
        return;
    }

    selection_clear();
    page_cache_init(&state->page_cache);
    page_cache_add(&state->page_cache, 0, state->screen_cols, state->content_rows);

    load_page_at_offset(0);
    state->current_page = 0;
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;

    // Preload next page if available
    if (state->total_pages > 1) {
        long next_offset = estimate_next_page_offset(state->file, 0, state->lines_per_page);
        if (next_offset > 0) {
            page_cache_add(&state->page_cache, next_offset, state->screen_cols, state->content_rows);
        }
    }

    strcpy(state->statusmsg, "First page");
}

// Navigate to last page
static void navigate_last_page(void) {
    if (!state->file) return;

    selection_clear();
    long current_offset = page_cache_current_pos(&state->page_cache);
    while (state->current_page < state->total_pages - 1) {
        long next_offset = estimate_next_page_offset(state->file, current_offset, state->lines_per_page);
        if (next_offset <= current_offset) break;

        page_cache_add(&state->page_cache, next_offset, state->screen_cols, state->content_rows);
        page_cache_next(&state->page_cache);
        current_offset = next_offset;
        state->current_page++;
    }

    load_page_at_offset(current_offset);
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;

    snprintf(state->statusmsg, sizeof(state->statusmsg), "Page %d/%d", state->current_page + 1, state->total_pages);
}

static void selection_clear(void) {
    state->selection_active = false;
}

static void selection_update(void) {
    if (state->selection_active) {
        state->sel_end_row = state->cursor_row;
        state->sel_end_col = state->cursor_col;
    }
}

static void normalize_selection(int *start_row, int *start_col, int *end_row, int *end_col) {
    if (state->sel_start_row < state->sel_end_row ||
        (state->sel_start_row == state->sel_end_row && state->sel_start_col < state->sel_end_col)) {
        *start_row = state->sel_start_row;
        *start_col = state->sel_start_col;
        *end_row = state->sel_end_row;
        *end_col = state->sel_end_col;
    } else {
        *start_row = state->sel_end_row;
        *start_col = state->sel_end_col;
        *end_row = state->sel_start_row;
        *end_col = state->sel_start_col;
    }
}

static void selection_copy(void) {
    if (!state->selection_active || !state->clipboard_buffer) return;

    int start_row, start_col, end_row, end_col;
    normalize_selection(&start_row, &start_col, &end_row, &end_col);

    char *buf = state->clipboard_buffer;
    int pos = 0;
    for (int i = start_row; i <= end_row && i < state->line_count; i++) {
        if (!state->lines[i]) continue;
        int line_len = strlen(state->lines[i]);
        int from = (i == start_row) ? start_col : 0;
        int to = (i == end_row) ? end_col : line_len;
        if (to > line_len) to = line_len;
        int count = to - from;
        if (count > 0 && pos + count < MAX_CLIPBOARD_SIZE) {
            memcpy(buf + pos, state->lines[i] + from, count);
            pos += count;
        }
        if (i < end_row && pos < MAX_CLIPBOARD_SIZE - 1) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    state->clipboard_size = pos;
}

static void selection_delete(void) {
    if (!state->selection_active) return;

    int start_row, start_col, end_row, end_col;
    normalize_selection(&start_row, &start_col, &end_row, &end_col);

    if (start_row == end_row) {
        char *line = state->lines[start_row];
        if (line) {
            int len = strlen(line);
            int count = end_col - start_col;
            if (count > 0 && end_col <= len) {
                memmove(line + start_col, line + end_col, len - end_col + 1);
            }
        }
    } else {
        char *start_line = state->lines[start_row];
        char *end_line = state->lines[end_row];

        if (start_line) {
            start_line[start_col] = '\0';
        }

        if (start_line && end_line) {
            int tail_len = strlen(end_line) - end_col;
            if (tail_len > 0) {
                int new_len = start_col + tail_len;
                char *new_line = realloc(start_line, new_len + 1);
                if (new_line) {
                    state->lines[start_row] = new_line;
                    memcpy(new_line + start_col, end_line + end_col, tail_len);
                    new_line[new_len] = '\0';
                }
            }
        }

        if (end_line) free(end_line);

        int remove_count = end_row - start_row;
        for (int i = start_row + 1; i + remove_count < state->line_count; i++) {
            state->lines[i] = state->lines[i + remove_count];
        }
        state->line_count -= remove_count;
    }

    state->cursor_row = start_row;
    state->cursor_col = start_col;
    state->dirty = 1;
    selection_clear();
}

static void paste_from_clipboard(void) {
    if (!state->clipboard_buffer || state->clipboard_size <= 0) {
        strcpy(state->statusmsg, "Nothing to paste");
        return;
    }

    // Delete selection if active (but keep clipboard intact)
    bool had_selection = state->selection_active;
    if (had_selection) {
        int saved_start_row = state->sel_start_row;
        int saved_start_col = state->sel_start_col;
        int saved_end_row = state->sel_end_row;
        int saved_end_col = state->sel_end_col;
        selection_delete();
        // restore selection for copy after we finish... actually just delete and move on
    }

    char *buf = state->clipboard_buffer;
    int buf_len = state->clipboard_size;

    // Count lines in clipboard
    int paste_lines = 1;
    for (int i = 0; i < buf_len; i++) {
        if (buf[i] == '\n') paste_lines++;
    }

    // Check if we have room
    if (state->line_count + paste_lines - 1 >= state->line_alloc) {
        strcpy(state->statusmsg, "Paste would exceed line limit");
        return;
    }

    // Save the tail of the current line
    char *current_line = state->lines[state->cursor_row];
    int cur_len = current_line ? strlen(current_line) : 0;
    char *tail = NULL;
    int tail_len = cur_len - state->cursor_col;
    if (tail_len > 0) {
        tail = malloc(tail_len + 1);
        if (tail) {
            memcpy(tail, current_line + state->cursor_col, tail_len);
            tail[tail_len] = '\0';
        }
    }

    // Truncate current line at cursor
    if (current_line) {
        current_line[state->cursor_col] = '\0';
    }

    // Parse clipboard into lines
    char *paste_parts[128];
    int paste_count = 0;
    int part_start = 0;
    for (int i = 0; i <= buf_len && paste_count < 128; i++) {
        if (i == buf_len || buf[i] == '\n') {
            int part_len = i - part_start;
            paste_parts[paste_count] = malloc(part_len + 1);
            if (paste_parts[paste_count]) {
                memcpy(paste_parts[paste_count], buf + part_start, part_len);
                paste_parts[paste_count][part_len] = '\0';
            }
            paste_count++;
            part_start = i + 1;
        }
    }

    if (paste_count == 0) {
        free(tail);
        return;
    }

    // Extend first line with first paste part
    if (paste_parts[0]) {
        int first_len = strlen(paste_parts[0]);
        char *new_line = realloc(state->lines[state->cursor_row], state->cursor_col + first_len + 1);
        if (new_line) {
            state->lines[state->cursor_row] = new_line;
            memcpy(new_line + state->cursor_col, paste_parts[0], first_len);
            new_line[state->cursor_col + first_len] = '\0';
        }
    }

    // Insert middle lines (no tail)
    int insert_row = state->cursor_row;
    for (int p = 1; p < paste_count; p++) {
        // Shift lines down
        for (int j = state->line_count; j > insert_row + 1; j--) {
            state->lines[j] = state->lines[j - 1];
        }
        state->line_count++;

        int part_len = strlen(paste_parts[p]);
        char *paste_line = malloc(part_len + 1);
        if (paste_line) {
            memcpy(paste_line, paste_parts[p], part_len + 1);
        } else {
            paste_line = malloc(1);
            if (paste_line) paste_line[0] = '\0';
        }
        state->lines[insert_row + 1] = paste_line;
        insert_row++;
    }

    // Append tail to last inserted line
    if (tail && tail_len > 0) {
        char *last_line = state->lines[insert_row];
        int last_len = last_line ? strlen(last_line) : 0;
        char *extended = realloc(last_line, last_len + tail_len + 1);
        if (extended) {
            state->lines[insert_row] = extended;
            memcpy(extended + last_len, tail, tail_len);
            extended[last_len + tail_len] = '\0';
        }
        free(tail);
    }

    // Set cursor position
    state->cursor_row = insert_row;
    state->cursor_col = state->lines[insert_row] ? strlen(state->lines[insert_row]) - (tail ? tail_len : 0) : 0;
    if (state->cursor_col < 0) state->cursor_col = 0;

    // Free temp paste parts
    for (int p = 0; p < paste_count; p++) {
        free(paste_parts[p]);
    }

    state->dirty = 1;
    snprintf(state->statusmsg, sizeof(state->statusmsg), "Pasted %d lines", paste_lines);
}

// Handle keyboard event
static void handle_keyboard_event(app_context_t *ctx, event_t *event) {
    // Only handle key press events, not release
    if (!event->keyboard.pressed) {
        return;
    }

    uint8_t key = event->keyboard.raw_key_code;
    uint8_t modifier = event->keyboard.modifiers;
    char ch = event->keyboard.key;

    os_log(TAG, "KB event: key=%d raw=0x%02x mod=0x%02x",
           ch, key, modifier);

    // Handle interactive prompt input mode first
    if (state->prompt_mode) {
        if (ch == 27) { // ESC key
            state->prompt_mode = false;
            strcpy(state->statusmsg, "Canceled");
            return;
        }
        if (ch == '\b' || key == KEY_BACKSPACE) {
            if (state->prompt_len > 0) {
                state->prompt_len--;
                state->prompt_buf[state->prompt_len] = '\0';
            }
            return;
        }
        if (ch == '\n' || key == KEY_ENTER) {
            state->prompt_mode = false;
            if (state->prompt_callback) {
                state->prompt_callback(state->prompt_buf);
            }
            return;
        }
        if (ch >= 32 && ch <= 126) {
            if (state->prompt_len < sizeof(state->prompt_buf) - 1) {
                state->prompt_buf[state->prompt_len++] = ch;
                state->prompt_buf[state->prompt_len] = '\0';
            }
            return;
        }
        return;
    }

    // Function key combinations using Ctrl (SYMBOL key)
    if (modifier & MODIFIER_CTRL) {
        switch (ch) {
            case 'n':
            case 'N':
                new_file();
                return;
            case 's':
            case 'S':
                save_file();
                return;
            case 'o':
            case 'O':
                if (config_bind_app("file_picker")) {
                    config_set_string("root_path", "/sdcard");
                    config_set_string("start_path", "/sdcard");
                    config_set_string("glob", "*.txt,*.c,*.h,*.md,*.cfg");
                    config_set_string("title", "Open File");
                    config_set_string("return_app", "hecto");
                    config_set_string("target_app", "hecto");
                    config_set_string("result_key", "open_filepath");
                    config_set_int("cancel_to_launcher", 0);
                    config_unbind_app();

                    // Save checkpoint first
                    app_checkpoint(ctx);

                    os_load_app("file_picker");
                }
                return;
            case 'c':
            case 'C':
                selection_copy();
                if (state->selection_active) {
                    snprintf(state->statusmsg, sizeof(state->statusmsg), "Copied %d bytes", state->clipboard_size);
                    state->selection_active = false;
                } else {
                    strcpy(state->statusmsg, "Nothing to copy");
                }
                return;
            case 'x':
            case 'X':
                selection_copy();
                if (state->selection_active) {
                    selection_delete();
                    snprintf(state->statusmsg, sizeof(state->statusmsg), "Cut %d bytes", state->clipboard_size);
                } else {
                    strcpy(state->statusmsg, "Nothing to cut");
                }
                return;
            case 'v':
            case 'V':
                paste_from_clipboard();
                return;
            case 'q':
            case 'Q':
                os_exit();
                return;
            case 'h':
            case 'H':
                strcpy(state->statusmsg, "Help: Ctrl+S=Save Ctrl+N=New Ctrl+Q=Quit Ctrl+C=Copy Ctrl+X=Cut Ctrl+V=Paste");
                return;
        }
    }

    // Handle Fn+WASD for arrow keys (like kilo)
    // Fn2 acts as shift for text selection
    if (modifier & MODIFIER_FN2) {
        if (!state->selection_active) {
            state->selection_active = true;
            state->sel_start_row = state->cursor_row;
            state->sel_start_col = state->cursor_col;
            state->sel_end_row = state->cursor_row;
            state->sel_end_col = state->cursor_col;
        }
    } else {
        selection_clear();
    }

    if (modifier & (MODIFIER_FN | MODIFIER_FN2)) {
        switch (event->keyboard.key) {
            case 'w':
            case 'W':
                os_log(TAG, "Fn+W (UP)");
                if (state->cursor_row > 0) {
                    state->cursor_row--;
                    // Adjust cursor column if it's beyond the new line's end
                    if (state->lines[state->cursor_row]) {
                        int new_len = strlen(state->lines[state->cursor_row]);
                        if (state->cursor_col > new_len) {
                            state->cursor_col = new_len;
                        }
                    }
                    if (state->cursor_row < state->scroll_offset) {
                        state->scroll_offset--;
                    }
                } else if (state->file && state->current_page > 0) {
                    navigate_prev_page();
                }
                break;
            case 's':
            case 'S':
                os_log(TAG, "Fn+S (DOWN)");
                if (state->cursor_row < state->line_count - 1) {
                    state->cursor_row++;
                    // Adjust cursor column if it's beyond the new line's end
                    if (state->lines[state->cursor_row]) {
                        int new_len = strlen(state->lines[state->cursor_row]);
                        if (state->cursor_col > new_len) {
                            state->cursor_col = new_len;
                        }
                    }
                    if (state->cursor_row - state->scroll_offset >= state->content_rows) {
                        state->scroll_offset++;
                    }
                } else if (state->file && state->current_page < state->total_pages - 1) {
                    navigate_next_page();
                }
                break;
            case 'a':
            case 'A':
                os_log(TAG, "Fn+A (LEFT)");
                if (state->cursor_col > 0) {
                    state->cursor_col--;
                    // Scroll left if cursor is left of visible area
                    if (state->cursor_col < state->col_offset) {
                        state->col_offset = state->cursor_col;
                    }
                }
                break;
            case 'd':
            case 'D':
                os_log(TAG, "Fn+D (RIGHT)");
                state->cursor_col++;
                os_log(TAG, "cursor_col=%d col_offset=%d screen_cols=%d",
                       state->cursor_col, state->col_offset, state->screen_cols);

                // Scroll right if cursor is beyond visible area (keep cursor visible)
                if (state->cursor_col >= state->col_offset + state->screen_cols - 2) {
                    state->col_offset = state->cursor_col - state->screen_cols + 2;
                    os_log(TAG, "SCROLLED: new col_offset=%d", state->col_offset);
                }
                break;
        }
        selection_update();
        return;
    }

    // Handle selection for arrow keys with Fn2
    if (modifier & MODIFIER_FN2) {
        if (!state->selection_active) {
            state->selection_active = true;
            state->sel_start_row = state->cursor_row;
            state->sel_start_col = state->cursor_col;
            state->sel_end_row = state->cursor_row;
            state->sel_end_col = state->cursor_col;
        }
    } else {
        selection_clear();
    }

    // Handle actual arrow keys if available
    switch (key) {
        case KEY_LEFT:
            os_log(TAG, "LEFT key");
            if (state->cursor_col > 0) {
                state->cursor_col--;
                // Scroll left if cursor is left of visible area
                if (state->cursor_col < state->col_offset) {
                    state->col_offset = state->cursor_col;
                }
                selection_update();
            }
            break;
        case KEY_RIGHT:
            os_log(TAG, "RIGHT key");
            state->cursor_col++;
            os_log(TAG, "cursor_col=%d col_offset=%d screen_cols=%d",
                   state->cursor_col, state->col_offset, state->screen_cols);

            // Scroll right if cursor is beyond visible area (keep cursor visible)
            if (state->cursor_col >= state->col_offset + state->screen_cols - 2) {
                state->col_offset = state->cursor_col - state->screen_cols + 2;
                os_log(TAG, "SCROLLED: new col_offset=%d", state->col_offset);
            }
            selection_update();
            break;
        case KEY_UP:
            os_log(TAG, "UP key");
            if (state->cursor_row > 0) {
                state->cursor_row--;
                // Adjust cursor column if it's beyond the new line's end
                if (state->lines[state->cursor_row]) {
                    int new_len = strlen(state->lines[state->cursor_row]);
                    if (state->cursor_col > new_len) {
                        state->cursor_col = new_len;
                    }
                }
                if (state->cursor_row < state->scroll_offset) {
                    state->scroll_offset--;
                }
                selection_update();
            } else if (state->file && state->current_page > 0) {
                navigate_prev_page();
            }
            break;
        case KEY_DOWN:
            os_log(TAG, "DOWN key");
            if (state->cursor_row < state->line_count - 1) {
                state->cursor_row++;
                // Adjust cursor column if it's beyond the new line's end
                if (state->lines[state->cursor_row]) {
                    int new_len = strlen(state->lines[state->cursor_row]);
                    if (state->cursor_col > new_len) {
                        state->cursor_col = new_len;
                    }
                }
                if (state->cursor_row - state->scroll_offset >= state->content_rows) {
                    state->scroll_offset++;
                }
                selection_update();
            } else if (state->file && state->current_page < state->total_pages - 1) {
                navigate_next_page();
            }
            break;
        case KEY_PGUP:
            os_log(TAG, "PGUP key");
            navigate_prev_page();
            break;
        case KEY_PGDN:
            os_log(TAG, "PGDN key");
            navigate_next_page();
            break;
        case KEY_HOME:
            os_log(TAG, "HOME key");
            if (modifier & MODIFIER_CTRL) {
                navigate_first_page();
            } else {
                state->cursor_col = 0;
            }
            break;
        case KEY_END:
            os_log(TAG, "END key");
            if (modifier & MODIFIER_CTRL) {
                navigate_last_page();
            } else {
                // Move to end of current line
                if (state->cursor_row < state->line_count && state->lines[state->cursor_row]) {
                    state->cursor_col = strlen(state->lines[state->cursor_row]);
                }
            }
            break;
        default:
            // Regular character key - insert into text
            if (event->keyboard.key >= 32 && event->keyboard.key <= 126) {
                // Check if we're transitioning from empty to having content
                bool was_empty = (state->line_count == 1 && state->lines[0] && state->lines[0][0] == '\0');

                // Clear selection when typing
                selection_clear();

                // Validate cursor position first
                if (state->cursor_row >= state->line_count) {
                    os_log(TAG, "Invalid cursor row %d >= count %d", state->cursor_row, state->line_count);
                    return;
                }

                if (!state->lines[state->cursor_row]) {
                    os_log(TAG, "Null line at row %d", state->cursor_row);
                    return;
                }

                // Insert character at cursor position
                char *line = state->lines[state->cursor_row];
                int len = strlen(line);

                os_log(TAG, "Inserting '%c' at row=%d col=%d line='%s' (len=%d)",
                       event->keyboard.key, state->cursor_row, state->cursor_col, line, len);

                // Ensure cursor position is valid
                if (state->cursor_col > len) {
                    state->cursor_col = len;
                    os_log(TAG, "Adjusted cursor to %d", state->cursor_col);
                }

                // Safety limit on line length
                if (len > 200) {
                    os_log(TAG, "Line too long (%d chars), refusing insert", len);
                    return;
                }

                // Reallocate line to make room for new character
                char *new_line = realloc(line, len + 2);
                if (new_line) {
                    state->lines[state->cursor_row] = new_line;

                    // Insert character at cursor position
                    // Move everything from cursor_col to end (including null terminator) one position right
                    memmove(&new_line[state->cursor_col + 1], &new_line[state->cursor_col], len - state->cursor_col + 1);
                    new_line[state->cursor_col] = event->keyboard.key;

                    state->cursor_col++;

                    // Trigger horizontal scroll if needed during typing (keep cursor visible)
                    if (state->cursor_col >= state->col_offset + state->screen_cols - 2) {
                        state->col_offset = state->cursor_col - state->screen_cols + 2;
                        os_log(TAG, "Auto-scroll during typing: col_offset=%d cursor_col=%d screen_cols=%d",
                               state->col_offset, state->cursor_col, state->screen_cols);
                    }

                    state->dirty = 1;

                    os_log(TAG, "After insert: line='%s' (len=%d)", new_line, strlen(new_line));
                } else {
                    os_log(TAG, "Failed to realloc line");
                }

                // If we just transitioned from empty, do a full render to clear the help screen
                if (was_empty) {
                    text_mode_clear(TEXT_COLOR_BLACK);
                    render_menu_bar();
                    render_content();
                    render_status_bar();
                    text_mode_flush();
                    return; // Skip the normal render since we just did a full one
                }
            }
            break;
        case KEY_DELETE:
            os_log(TAG, "DELETE key");
            selection_clear();
            if (state->cursor_row < state->line_count && state->lines[state->cursor_row]) {
                char *line = state->lines[state->cursor_row];
                int len = strlen(line);

                if (state->cursor_col < len) {
                    // Delete character at cursor position
                    memmove(&line[state->cursor_col], &line[state->cursor_col + 1], len - state->cursor_col);
                    state->dirty = 1;
                    os_log(TAG, "Deleted char at %d,%d", state->cursor_row, state->cursor_col);
                }
            }
            break;
        case KEY_BACKSPACE:
            os_log(TAG, "BACKSPACE key");
            selection_clear();
            if (state->cursor_row < state->line_count && state->lines[state->cursor_row]) {
                char *line = state->lines[state->cursor_row];
                int len = strlen(line);

                if (state->cursor_col > 0 && len > 0) {
                    // Delete character before cursor (safe version)
                    memmove(&line[state->cursor_col - 1], &line[state->cursor_col], len - state->cursor_col + 1);
                    state->cursor_col--;
                    state->dirty = 1;
                    os_log(TAG, "Backspace at %d,%d", state->cursor_row, state->cursor_col);
                } else if (state->cursor_col == 0 && state->cursor_row > 0) {
                    // Join with previous line (basic implementation)
                    // For now, just move to end of previous line
                    state->cursor_row--;
                    if (state->lines[state->cursor_row]) {
                        state->cursor_col = strlen(state->lines[state->cursor_row]);
                    }
                }
            }
            break;
        case KEY_ENTER:
            os_log(TAG, "ENTER key");
            selection_clear();
            if (state->cursor_row < state->line_count) {
                // Split line at cursor position
                char *current_line = state->lines[state->cursor_row];
                int len = current_line ? strlen(current_line) : 0;

                // Make room for new line
                if (state->line_count < state->line_alloc - 1) {
                    // Move existing lines down
                    for (int i = state->line_count; i > state->cursor_row + 1; i--) {
                        state->lines[i] = state->lines[i - 1];
                    }

                    // Create new line with content after cursor
                    if (state->cursor_col < len) {
                        state->lines[state->cursor_row + 1] = strdup(&current_line[state->cursor_col]);
                        // Truncate current line
                        current_line[state->cursor_col] = '\0';
                    } else {
                        state->lines[state->cursor_row + 1] = malloc(1);
                        if (state->lines[state->cursor_row + 1]) {
                            state->lines[state->cursor_row + 1][0] = '\0';
                        }
                    }

                    state->line_count++;
                    state->cursor_row++;
                    state->cursor_col = 0;
                    state->dirty = 1;

                    os_log(TAG, "Split line at %d", state->cursor_row - 1);
                }
            }
            break;
    }
}

static void handle_touch_event(event_t *event) {
    if (!event->touch.pressed) {
        return;
    }

    int cell_x = 0, cell_y = 0;
    text_mode_pixel_to_cell(event->touch.x, event->touch.y, &cell_x, &cell_y);

    os_log(TAG, "Touch event cell: x=%d y=%d (pixel x=%d y=%d)", cell_x, cell_y, event->touch.x, event->touch.y);

    if (cell_y == 0) {
        // Menu bar clicked
        if (cell_x >= 1 && cell_x <= 4) { // "Open"
            event_t fake_ev = {
                .type = EVENT_KEYBOARD,
                .keyboard = {
                    .key = 'o',
                    .pressed = true,
                    .modifiers = MODIFIER_CTRL,
                    .raw_key_code = 0
                }
            };
            handle_keyboard_event(NULL, &fake_ev);
        } else if (cell_x >= 8 && cell_x <= 11) { // "Save"
            save_file();
        } else if (cell_x >= 15 && cell_x <= 18) { // "Help"
            strcpy(state->statusmsg, "Help: Tap text to place cursor. Open=Ctrl+O, Save=Ctrl+S.");
        }
    } else if (cell_y > 0 && cell_y < state->screen_rows - 1) {
        // Content area clicked - map to logical text cell
        int target_row = state->scroll_offset + (cell_y - 1);
        if (target_row < 0) target_row = 0;
        if (target_row >= state->line_count) {
            target_row = state->line_count - 1;
        }

        if (target_row >= 0 && target_row < state->line_count && state->lines[target_row]) {
            selection_clear();
            state->cursor_row = target_row;
            int len = strlen(state->lines[target_row]);
            int target_col = state->col_offset + cell_x;
            if (target_col < 0) target_col = 0;
            if (target_col > len) target_col = len;
            state->cursor_col = target_col;

            snprintf(state->statusmsg, sizeof(state->statusmsg), "Cursor placed at line %d", target_row + 1);
        }
    }
}

// App initialization
void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    ctx->timer_interval_ms = 0;

    init_state();

    // Check for startup file first
    char filename[256] = {0};
    size_t startup_len = os_consume_startup_file(filename, sizeof(filename));

    if (startup_len > 0 && filename[0]) {
        open_file(filename);
    } else {
        // Check config for last file or file picker result
        config_bind_app("hecto");
        char saved_file[256] = {0};

        // File picker result
        config_get_string("open_filepath", "", saved_file, sizeof(saved_file));
        if (saved_file[0]) {
            config_delete("open_filepath");
            open_file(saved_file);
        } else {
            // Restore last session
            config_get_string("editor_file", "", saved_file, sizeof(saved_file));
            if (saved_file[0]) {
                open_file(saved_file);
                int saved_offset = config_get_int("editor_offset", 0);
                if (saved_offset > 0 && saved_offset < state->line_count) {
                    state->cursor_row = saved_offset;
                }
            }
        }
        config_unbind_app();
    }

    // Initial render (with full clear since this is startup)
    text_mode_clear(TEXT_COLOR_BLACK);
    render_menu_bar();
    render_content();
    render_status_bar();
    text_mode_flush();

    os_log(TAG, "Hecto initialized");
}

// Handle events
void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD) {
        handle_keyboard_event(ctx, event);
    } else if (event->type == EVENT_TOUCH) {
        handle_touch_event(event);
    }

    // Render only what's needed (no full clear)
    render_menu_bar();
    render_content();
    render_status_bar();
    text_mode_flush();
}

// Checkpoint (save state)
void app_checkpoint(app_context_t *ctx) {
    // Save current file and cursor position
    if (state && state->filename[0]) {
        config_bind_app("hecto");
        config_set_string("editor_file", state->filename);
        config_set_int("editor_offset", state->cursor_row);
        config_unbind_app();
    }
}

// Close app
void app_close(app_context_t *ctx) {
    cleanup_state();
    text_mode_clear(TEXT_COLOR_BLACK);
}