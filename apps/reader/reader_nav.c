#include "reader_nav.h"

#include "reader_core.h"
#include "text_mode.h"
#include "ui2.h"
#include "ui_osk.h"
#include "os_core.h"
#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }
    return ch;
}

static void reader_nav_search_forward(reader_state_t *state, const char *query, int *bold_pending, int *underline_pending);
void on_goto_confirm(void *user_data);
void on_goto_cancel(void *user_data);
void on_search_confirm(void *user_data);
void on_search_cancel(void *user_data);

static int contains_substring_nocase(const char *text, const char *needle) {
    if (!needle || !needle[0]) {
        return 0;
    }

    size_t needle_len = strlen(needle);
    for (size_t start = 0; text[start]; start++) {
        size_t index = 0;
        while (index < needle_len && text[start + index] &&
               ascii_lower(text[start + index]) == ascii_lower(needle[index])) {
            index++;
        }
        if (index == needle_len) {
            return 1;
        }
    }

    return 0;
}

static int page_contains_query(const reader_state_t *state, const char *query) {
    for (int index = 0; index < state->line_count; index++) {
        if (contains_substring_nocase(state->lines[index].text, query)) {
            return 1;
        }
    }
    return 0;
}

// Scan one page from current file position using the token renderer.
// Returns number of content lines rendered, and sets next_pos_out to
// the file offset where the next page starts.
static int scan_one_page(FILE *file, rendered_line_t *lines, int max_lines, int screen_width, int line_buf_size, long *next_pos_out) {
    page_renderer_t renderer;
    renderer_init(&renderer, file, lines, NULL, max_lines, screen_width, line_buf_size);
    renderer.tokenizer.current_pos = ftell(file);
    if (!renderer_process_page(&renderer)) {
        return 0;
    }
    if (next_pos_out) {
        *next_pos_out = renderer_get_position(&renderer);
    }
    return renderer.line_count;
}

int reader_compute_page_number(reader_state_t *state) {
    uint32_t target_offset = state->page_cache.entries[state->page_cache.current].file_pos;
    if (target_offset == 0 || !state->file) {
        return 1;
    }

    // Use TOC to skip ahead — find the closest heading before the target offset
    int start_page = 1;
    long start_offset = 0;
    if (state->toc && state->toc_count > 0) {
        for (int i = state->toc_count - 1; i >= 0; i--) {
            if (state->toc[i].file_offset <= target_offset) {
                start_page = state->toc[i].page_number;
                start_offset = (long)state->toc[i].file_offset;
                break;
            }
        }
    }

    fseek(state->file, start_offset, SEEK_SET);
    int page = start_page;
    while (1) {
        long current_pos = ftell(state->file);
        if (current_pos >= (long)target_offset) {
            return page;
        }
        long next_pos;
        if (scan_one_page(state->file, state->lines, state->content_rows, state->screen_width, state->line_buf_size, &next_pos) == 0) {
            return page;
        }
        if (next_pos <= current_pos) {
            return page;
        }
        if ((long)target_offset < next_pos) {
            return page;
        }
        fseek(state->file, next_pos, SEEK_SET);
        page++;
    }
}

static void reader_nav_goto_page(reader_state_t *state, int target, int *bold_pending, int *underline_pending) {
    if (target < 1) {
        return;
    }

    fseek(state->file, 0, SEEK_SET);

    uint32_t page_starts[PAGE_CACHE_ENTRIES];
    int store_count = 0;

    int last_page = 0;
    int page;
    for (page = 1; page < target; page++) {
        long start = ftell(state->file);
        long next_pos;
        int line_count = scan_one_page(state->file, state->lines, state->content_rows, state->screen_width, state->line_buf_size, &next_pos);
        if (line_count == 0) {
            break;
        }

        last_page = page;
        if (store_count < PAGE_CACHE_ENTRIES) {
            page_starts[store_count++] = (uint32_t)start;
        } else {
            // Buffer full — shift left (drop oldest), append newest
            memmove(page_starts, page_starts + 1, (PAGE_CACHE_ENTRIES - 1) * sizeof(uint32_t));
            page_starts[PAGE_CACHE_ENTRIES - 1] = (uint32_t)start;
        }

        if (next_pos <= start) break;
        fseek(state->file, next_pos, SEEK_SET);
    }

    uint32_t offset = 0;
    int actual_page = 1;

    if (last_page == 0) {
        offset = 0;
        actual_page = 1;
        fseek(state->file, 0, SEEK_SET);
    } else if (page < target) {
        offset = page_starts[store_count > 0 ? store_count - 1 : 0];
        actual_page = last_page;
        fseek(state->file, offset, SEEK_SET);
    } else {
        long probe_pos = ftell(state->file);
        long next_pos;
        int probe_count = scan_one_page(state->file, state->lines, state->content_rows, state->screen_width, state->line_buf_size, &next_pos);
        if (probe_count == 0) {
            offset = page_starts[store_count > 0 ? store_count - 1 : 0];
            actual_page = last_page;
            fseek(state->file, offset, SEEK_SET);
        } else {
            offset = (uint32_t)probe_pos;
            actual_page = target;
            fseek(state->file, probe_pos, SEEK_SET);
            if (store_count < PAGE_CACHE_ENTRIES) {
                page_starts[store_count++] = offset;
            } else {
                // Buffer full — shift left (drop oldest), add target at end
                memmove(page_starts, page_starts + 1, (PAGE_CACHE_ENTRIES - 1) * sizeof(uint32_t));
                page_starts[PAGE_CACHE_ENTRIES - 1] = offset;
            }
        }
    }

    page_cache_init(&state->page_cache);
    int cache_entries = store_count > PAGE_CACHE_ENTRIES ? PAGE_CACHE_ENTRIES : store_count;
    if (cache_entries > 0) {
        state->page_cache.count = cache_entries;
        state->page_cache.current = cache_entries - 1;
        for (int index = 0; index < cache_entries; index++) {
            state->page_cache.entries[index].file_pos = page_starts[index];
            state->page_cache.entries[index].screen_width = state->screen_width;
            state->page_cache.entries[index].content_rows = state->content_rows;
            state->page_cache.entries[index].state = RENDER_STATE_DEFAULT;
        }
    } else {
        state->page_cache.entries[0].file_pos = offset;
        state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
        state->page_cache.count = 1;
        state->page_cache.current = 0;
        state->page_cache.entries[0].screen_width = state->screen_width;
        state->page_cache.entries[0].content_rows = state->content_rows;
    }

    state->page_number = actual_page;
    reader_load_current_page(state, bold_pending, underline_pending);
    // Save progress after page change
    reader_save_current_book_progress(state);
}

void reader_nav_next_page(reader_state_t *state, int *bold_pending, int *underline_pending) {
    // Check if cache is still valid (dimensions haven't changed)
    if (!page_cache_is_valid(&state->page_cache, state->screen_width, state->content_rows)) {
        // Cache invalid - rebuild from current position
        uint32_t current_offset = ftell(state->file);
        printf("NAV_NEXT: Cache invalid, rebuilding from offset %u\n", current_offset);
        page_cache_init(&state->page_cache);
        state->page_cache.entries[0].file_pos = current_offset;
        state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
        state->page_cache.count = 1;
        state->page_cache.current = 0;
        state->page_cache.entries[0].screen_width = state->screen_width;
        state->page_cache.entries[0].content_rows = state->content_rows;
    }

    if (page_cache_can_next(&state->page_cache)) {
        printf("NAV_NEXT: Using cached page %d\n", state->page_cache.current + 1);
        page_cache_next(&state->page_cache);
    } else {
        // No cached next page. Render the current page to compute the next
        // page position (reader_load_current_page with the ring buffer fix
        // will store it properly).
        printf("NAV_NEXT: No cached next page, computing by rendering current page\n");
        reader_load_current_page(state, bold_pending, underline_pending);
        page_cache_next(&state->page_cache);
    }

    state->page_number++;
    printf("NAV_NEXT: Loading page %d from offset %ld\n", state->page_number, state->page_cache.entries[state->page_cache.current].file_pos);
    reader_load_current_page(state, bold_pending, underline_pending);
    // Save progress after page change
    reader_save_current_book_progress(state);
}

void reader_nav_prev_page(reader_state_t *state, int *bold_pending, int *underline_pending) {
    // Check if cache is still valid (dimensions haven't changed)
    if (!page_cache_is_valid(&state->page_cache, state->screen_width, state->content_rows)) {
        // Cache invalid - need to rebuild by going to specific page
        if (state->page_number > 1) {
            int cols = text_mode_get_cols();
            int rows = text_mode_get_rows();
            for (int row = 2; row < rows; row++) {
                for (int col = 0; col < cols; col++) {
                    text_mode_print_at_attr_bg(col, row, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
                }
            }
            text_mode_print_at_attr((cols - 10) / 2, rows / 2, "Loading...", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
            reader_nav_goto_page(state, state->page_number - 1, bold_pending, underline_pending);
        }
        return;
    }

    if (page_cache_can_prev(&state->page_cache)) {
        page_cache_prev(&state->page_cache);
        state->page_number--;
        reader_load_current_page(state, bold_pending, underline_pending);
        // Save progress after page change
        reader_save_current_book_progress(state);
        return;
    }

    if (state->page_number > 1) {
        int cols = text_mode_get_cols();
        int rows = text_mode_get_rows();
        for (int row = 2; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                text_mode_print_at_attr_bg(col, row, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
            }
        }
        text_mode_print_at_attr((cols - 10) / 2, rows / 2, "Loading...", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        reader_nav_goto_page(state, state->page_number - 1, bold_pending, underline_pending);
    }
}

void reader_nav_start_goto(reader_state_t *state) {
    state->mode = MODE_GOTO;
    state->goto_buf[0] = '\0';

    // Check if keyboard is available
    if (!keyboard_is_available()) {
        // Use OSK for touch input
        if (ui_osk_input_text("Go to Page:", state->goto_buf, sizeof(state->goto_buf), NULL, false)) {
            // OSK was started successfully, will be handled in event loop
        } else {
            // Fallback to normal mode if OSK fails
            state->mode = MODE_READING;
        }
        return;
    }

    // Create goto widget if it doesn't exist (physical keyboard available)
    if (!state->goto_widget) {
        int cols = text_mode_get_cols();
        int rows = text_mode_get_rows();

        state->goto_widget = ui2_text_input_create(0, rows - 4, cols, 4);
        ui2_text_input_set_title(state->goto_widget, "Go to Page");
        ui2_text_input_set_label(state->goto_widget, "Page:");
        ui2_text_input_set_hints(state->goto_widget, "Type number  Enter Confirm", "ESC Cancel");
        ui2_text_input_set_callbacks(state->goto_widget, on_goto_confirm, on_goto_cancel, state);
    }

    // Set buffer and redraw
    ui2_text_input_set_buffer(state->goto_widget, state->goto_buf, sizeof(state->goto_buf));
    UI2_WIDGET(state->goto_widget)->vtable->draw(UI2_WIDGET(state->goto_widget));
    text_mode_flush();
}

void reader_nav_handle_goto_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (!state->goto_widget) {
        return;
    }

    // Let the widget handle the key
    if (UI2_WIDGET(state->goto_widget)->vtable->handle_key(UI2_WIDGET(state->goto_widget), key)) {
        // Widget handled the key, only redraw if still in goto mode
        if (state->mode == MODE_GOTO) {
            UI2_WIDGET(state->goto_widget)->vtable->draw(UI2_WIDGET(state->goto_widget));
            text_mode_flush();
        }
    }
    // Callbacks handle mode switching and page navigation
}

void on_goto_confirm(void *user_data) {
    if (!user_data) return;
    reader_state_t *state = (reader_state_t *)user_data;
    int page = 0;
    if (state->goto_buf[0] != '\0') page = atoi(state->goto_buf);
    int bp = 0, up = 0;
    reader_nav_goto_page(state, (page > 1) ? page : 1, &bp, &up);
}

void on_goto_cancel(void *user_data) {
    if (!user_data) return;
    reader_state_t *state = (reader_state_t *)user_data;
    state->mode = MODE_READING;
}

void on_search_confirm(void *user_data) {
    if (!user_data) return;
    reader_state_t *state = (reader_state_t *)user_data;
    int bp = 0, up = 0;
    reader_nav_search_forward(state, state->search_buf, &bp, &up);
}

void on_search_cancel(void *user_data) {
    if (!user_data) return;
    reader_state_t *state = (reader_state_t *)user_data;
    state->mode = MODE_READING;
}

void reader_nav_start_search(reader_state_t *state) {
    state->mode = MODE_SEARCH;

    // Clear previous search buffer
    state->search_buf[0] = '\0';

    // Check if keyboard is available
    if (!keyboard_is_available()) {
        // Use OSK for touch input
        if (ui_osk_input_text("Search:", state->search_buf, sizeof(state->search_buf), NULL, false)) {
            // OSK was started successfully, will be handled in event loop
        } else {
            // Fallback to normal mode if OSK fails
            state->mode = MODE_READING;
        }
        return;
    }

    // Create search widget if it doesn't exist (physical keyboard available)
    if (!state->search_widget) {
        int cols = text_mode_get_cols();
        int rows = text_mode_get_rows();

        state->search_widget = ui2_text_input_create(0, rows - 4, cols, 4);
        ui2_text_input_set_title(state->search_widget, "Search Forward");
        ui2_text_input_set_label(state->search_widget, "Text:");
        ui2_text_input_set_hints(state->search_widget, "Type text  Enter Search", "ESC Cancel");
        ui2_text_input_set_callbacks(state->search_widget, on_search_confirm, on_search_cancel, state);
    }

    // Set buffer and redraw
    ui2_text_input_set_buffer(state->search_widget, state->search_buf, sizeof(state->search_buf));
    UI2_WIDGET(state->search_widget)->vtable->draw(UI2_WIDGET(state->search_widget));
    text_mode_flush();
}

static void reader_nav_search_forward(reader_state_t *state, const char *query, int *bold_pending, int *underline_pending) {
    if (!query || !query[0] || !state->file) {
        snprintf(state->search_status, sizeof(state->search_status), "Search text is empty");
        return;
    }

    uint32_t start_offset = state->page_cache.entries[state->page_cache.current].file_pos;
    int start_page = state->page_number;

    // Start from the NEXT page to avoid finding the same result again
    fseek(state->file, start_offset, SEEK_SET);
    // Skip the current page by advancing past it
    long skip_next;
    scan_one_page(state->file, state->lines, state->content_rows, state->screen_width, state->line_buf_size, &skip_next);
    if (skip_next > 0) {
        fseek(state->file, skip_next, SEEK_SET);
    }

    int page = start_page + 1;  // Start from next page
    while (1) {
        uint32_t page_offset = (uint32_t)ftell(state->file);
        long next_pos;
        int line_count = scan_one_page(state->file, state->lines, state->content_rows, state->screen_width, state->line_buf_size, &next_pos);
        if (line_count == 0) {
            break;
        }

        state->line_count = line_count;
        if (page_contains_query(state, query)) {
            page_cache_init(&state->page_cache);
            state->page_cache.entries[0].file_pos = page_offset;
            state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
            state->page_cache.count = 1;
            state->page_cache.current = 0;
            state->page_cache.entries[0].screen_width = state->screen_width;
            state->page_cache.entries[0].content_rows = state->content_rows;
            state->page_number = page;
            reader_load_current_page(state, bold_pending, underline_pending);
            snprintf(state->search_status, sizeof(state->search_status), "Found \"%s\" on page %d", query, page);
            return;
        }

        if (next_pos <= page_offset) break;
        fseek(state->file, next_pos, SEEK_SET);
        page++;
    }

    fseek(state->file, start_offset, SEEK_SET);
    state->page_number = start_page;
    reader_load_current_page(state, bold_pending, underline_pending);
    snprintf(state->search_status, sizeof(state->search_status), "Not found: \"%s\"", query);
}

void reader_nav_handle_search_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (!state->search_widget) {
        return;
    }

    // Let the widget handle the key
    if (UI2_WIDGET(state->search_widget)->vtable->handle_key(UI2_WIDGET(state->search_widget), key)) {
        // Widget handled the key, only redraw if still in search mode
        if (state->mode == MODE_SEARCH) {
            UI2_WIDGET(state->search_widget)->vtable->draw(UI2_WIDGET(state->search_widget));
            text_mode_flush();
        }
    }
    // Callbacks handle mode switching and search execution
}
