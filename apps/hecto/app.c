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
static void handle_keyboard_event(event_t *event);
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

    memset(state, 0, sizeof(editor_state_t));

    // Get screen dimensions
    state->screen_cols = text_mode_get_cols();
    state->screen_rows = text_mode_get_rows();
    state->content_rows = state->screen_rows - 2; // Menu bar + status bar

    // Initialize page cache
    page_cache_init(&state->page_cache);

    // Allocate line buffer
    state->line_alloc = state->content_rows + 10;
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
    text_mode_print_at_attr_bg(0, 0, "File", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at(5, 0, " ");
    text_mode_print_at_attr_bg(6, 0, "Edit", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at(11, 0, " ");
    text_mode_print_at_attr_bg(12, 0, "View", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at(17, 0, " ");
    text_mode_print_at_attr_bg(18, 0, "Search", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at(25, 0, " ");
    text_mode_print_at_attr_bg(26, 0, "Help", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    // Fill rest with spaces
    for (int x = 31; x < state->screen_cols; x++) {
        text_mode_print_at(x, 0, " ");
    }
}

// Render status bar
static void render_status_bar(void) {
    int y = state->screen_rows - 1;

    // Clear status bar
    for (int x = 0; x < state->screen_cols; x++) {
        text_mode_print_at(x, y, " ");
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

            // Render the visible portion of the line
            if (visible_len > 0) {
                for (int x = 0; x < visible_len; x++) {
                    char ch[2] = {state->lines[i][start_col + x], '\0'};
                    text_mode_print_at(x, display_y, ch);
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
    int lines_to_read = state->content_rows + 10; // Read a few extra lines
    if (lines_to_read > state->line_alloc) {
        lines_to_read = state->line_alloc;
    }

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
    state->total_pages = calculate_total_pages(state->file, state->content_rows);
    state->current_page = 0;

    // Initialize page cache
    page_cache_init(&state->page_cache);
    page_cache_add(&state->page_cache, 0, state->screen_cols, state->content_rows);

    // Load first page
    load_page_at_offset(0);

    // Preload next page if available
    if (state->total_pages > 1) {
        long next_offset = estimate_next_page_offset(state->file, 0, state->content_rows);
        if (next_offset > 0) {
            page_cache_add(&state->page_cache, next_offset, state->screen_cols, state->content_rows);
        }
    }

    // Reset cursor
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;
    state->dirty = 0;

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

    // Reset cursor
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

// Save file as (placeholder for now)
static void save_file_as(void) {
    strcpy(state->statusmsg, "Save as: Not implemented yet");
}

// Navigate to next page
static void navigate_next_page(void) {
    if (!state->file || state->current_page >= state->total_pages - 1) {
        strcpy(state->statusmsg, "Already at last page");
        return;
    }

    // Check if next page is in cache
    if (page_cache_can_next(&state->page_cache)) {
        page_cache_next(&state->page_cache);
        long offset = page_cache_current_pos(&state->page_cache);
        load_page_at_offset(offset);
        state->current_page++;
    } else {
        // Need to load new page - estimate offset
        long current_offset = page_cache_current_pos(&state->page_cache);
        long next_offset = estimate_next_page_offset(state->file, current_offset, state->content_rows);

        if (next_offset > current_offset) {
            // Add to cache
            if (page_cache_add(&state->page_cache, next_offset, state->screen_cols, state->content_rows)) {
                page_cache_next(&state->page_cache);
                load_page_at_offset(next_offset);
                state->current_page++;

                // Preload next page if available
                if (state->current_page < state->total_pages - 1) {
                    long future_offset = estimate_next_page_offset(state->file, next_offset, state->content_rows);
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

    page_cache_init(&state->page_cache);
    page_cache_add(&state->page_cache, 0, state->screen_cols, state->content_rows);

    load_page_at_offset(0);
    state->current_page = 0;
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;

    // Preload next page if available
    if (state->total_pages > 1) {
        long next_offset = estimate_next_page_offset(state->file, 0, state->content_rows);
        if (next_offset > 0) {
            page_cache_add(&state->page_cache, next_offset, state->screen_cols, state->content_rows);
        }
    }

    strcpy(state->statusmsg, "First page");
}

// Navigate to last page
static void navigate_last_page(void) {
    if (!state->file || state->current_page == state->total_pages - 1) {
        return;
    }

    // For last page, we need to seek near end and work backwards
    // This is a simplified version
    strcpy(state->statusmsg, "Last page: Not fully implemented");
}

// Handle keyboard event
static void handle_keyboard_event(event_t *event) {
    // Only handle key press events, not release
    if (!event->keyboard.pressed) {
        return;
    }

    uint8_t key = event->keyboard.raw_key_code;
    uint8_t modifier = event->keyboard.modifiers;

    os_log(TAG, "KB event: key=%d raw=0x%02x mod=0x%02x",
           event->keyboard.key, key, modifier);

    // Function key combinations using Ctrl (SYMBOL key)
    if (modifier & MODIFIER_CTRL) {
        switch (event->keyboard.key) {
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
                strcpy(state->statusmsg, "Open: Use file picker");
                return;
            case 'q':
            case 'Q':
                os_exit();
                return;
            case 'h':
            case 'H':
                strcpy(state->statusmsg, "Help: Ctrl+S=Save Ctrl+N=New Ctrl+Q=Quit");
                return;
        }
    }

    // Handle Fn+WASD for arrow keys (like kilo)
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
                }
                return; // Return to prevent further processing
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
                }
                return; // Return to prevent further processing
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
                return; // Return to prevent further processing
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
                return; // Return to prevent further processing
        }
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

// App initialization
void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    init_state();

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
        handle_keyboard_event(event);

        // Render only what's needed (no full clear)
        render_menu_bar();
        render_content();
        render_status_bar();
        text_mode_flush();
    }
}

// Checkpoint (save state)
void app_checkpoint(app_context_t *ctx) {
    // Save current file and cursor position
    if (state && state->filename[0]) {
        config_bind_app("editor2");
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