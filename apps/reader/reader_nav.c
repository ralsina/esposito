#include "reader_nav.h"

#include "reader_core.h"
#include "reader_md.h"
#include "reader_view.h"
#include "text_mode.h"
#include "ui.h"
#include "os_core.h"

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
static void on_goto_confirm(ui_text_input_widget_t *widget, void *user_data);
static void on_goto_cancel(ui_text_input_widget_t *widget, void *user_data);
static void on_search_confirm(ui_text_input_widget_t *widget, void *user_data);
static void on_search_cancel(ui_text_input_widget_t *widget, void *user_data);

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

static void reader_nav_goto_page(reader_state_t *state, int target, int *bold_pending, int *underline_pending) {
    if (target < 1) {
        return;
    }

    md_clear_remainder();
    fseek(state->file, 0, SEEK_SET);

    uint32_t page_starts[PAGE_CACHE_ENTRIES];
    int store_count = 0;
    int store_pos = 0;

    int last_page = 0;
    int page = 1;
    for (page = 1; page < target; page++) {
        uint32_t start = ftell(state->file);
        int line_count = md_scan_page(state->file, state->lines, state->content_rows, state->screen_width);
        if (line_count == 0) {
            break;
        }

        last_page = page;
        page_starts[store_pos] = start;
        store_pos = (store_pos + 1) % PAGE_CACHE_ENTRIES;
        if (store_count < PAGE_CACHE_ENTRIES) {
            store_count++;
        }
    }

    uint32_t offset = 0;
    int actual_page = 1;

    if (last_page == 0) {
        offset = 0;
        actual_page = 1;
        fseek(state->file, 0, SEEK_SET);
    } else if (page < target) {
        int idx = (store_pos - 1 + PAGE_CACHE_ENTRIES) % PAGE_CACHE_ENTRIES;
        offset = page_starts[idx];
        actual_page = last_page;
        fseek(state->file, offset, SEEK_SET);
    } else {
        long probe_pos = ftell(state->file);
        md_clear_remainder();
        int probe_count = md_scan_page(state->file, state->lines, state->content_rows, state->screen_width);
        if (probe_count == 0) {
            int idx = (store_pos - 1 + PAGE_CACHE_ENTRIES) % PAGE_CACHE_ENTRIES;
            offset = page_starts[idx];
            actual_page = last_page;
            fseek(state->file, offset, SEEK_SET);
        } else {
            offset = (uint32_t)probe_pos;
            actual_page = target;
            fseek(state->file, probe_pos, SEEK_SET);

            page_starts[store_pos] = offset;
            store_pos = (store_pos + 1) % PAGE_CACHE_ENTRIES;
            if (store_count < PAGE_CACHE_ENTRIES) {
                store_count++;
            }
        }
    }

    page_cache_init(&state->page_cache);
    if (store_count > 0) {
        state->page_cache.count = store_count;
        state->page_cache.current = store_count - 1;
        for (int index = 0; index < store_count; index++) {
            int src = (store_pos - store_count + index + PAGE_CACHE_ENTRIES) % PAGE_CACHE_ENTRIES;
            state->page_cache.offsets[index] = page_starts[src];
        }
    } else {
        page_cache_set_start(&state->page_cache, offset);
    }

    md_clear_remainder();
    state->page_number = actual_page;
    reader_load_current_page(state, bold_pending, underline_pending);
    reader_view_draw_reading_page(state, bold_pending, underline_pending);
}

void reader_nav_next_page(reader_state_t *state, int *bold_pending, int *underline_pending) {
    if (page_cache_can_next(&state->page_cache)) {
        page_cache_next(&state->page_cache);
    } else {
        uint32_t next_offset = ftell(state->file);
        page_cache_add_next(&state->page_cache, next_offset);
        page_cache_next(&state->page_cache);
    }

    state->page_number++;
    reader_load_current_page(state, bold_pending, underline_pending);
    reader_view_draw_reading_page(state, bold_pending, underline_pending);
}

void reader_nav_prev_page(reader_state_t *state, int *bold_pending, int *underline_pending) {
    if (page_cache_can_prev(&state->page_cache)) {
        page_cache_prev(&state->page_cache);
        state->page_number--;
        reader_load_current_page(state, bold_pending, underline_pending);
        reader_view_draw_reading_page(state, bold_pending, underline_pending);
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

    // Create goto widget if it doesn't exist
    if (!state->goto_widget) {
        int cols = text_mode_get_cols();
        int rows = text_mode_get_rows();

        state->goto_widget = ui_text_input_create(0, rows - 4, cols, 4);
        ui_text_input_set_title(state->goto_widget, "Go to Page");
        ui_text_input_set_label(state->goto_widget, "Page:");
        ui_text_input_set_hints(state->goto_widget, "Type number  Enter Confirm", "ESC Cancel");
        ui_text_input_set_callbacks(state->goto_widget, NULL, on_goto_confirm, on_goto_cancel, state);
    }

    // Set buffer and redraw
    ui_text_input_set_buffer(state->goto_widget, state->goto_buf, sizeof(state->goto_buf));
    ui_text_input_draw(state->goto_widget);
    text_mode_flush();
}

void reader_nav_handle_goto_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (!state->goto_widget) {
        return;
    }

    // Let the widget handle the key
    if (ui_text_input_handle_key(state->goto_widget, key)) {
        // Widget handled the key, only redraw if still in goto mode
        if (state->mode == MODE_GOTO) {
            ui_text_input_draw(state->goto_widget);
            text_mode_flush();
        }
    }
    // Callbacks handle mode switching and page navigation
}

static void on_goto_confirm(ui_text_input_widget_t *widget, void *user_data) {
    (void)widget;
    if (!user_data) {
        return;
    }
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;

    // Parse and go to page
    int page = 0;
    if (state->goto_buf[0] != '\0') {
        page = atoi(state->goto_buf);
    }
    reader_nav_goto_page(state, (page > 1) ? page : 1, &bold_pending, &underline_pending);
}

static void on_goto_cancel(ui_text_input_widget_t *widget, void *user_data) {
    (void)widget;
    if (!user_data) {
        return;
    }
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;

    // Return to reading mode
    state->mode = MODE_READING;
    reader_view_draw_reading_page(state, &bold_pending, &underline_pending);
    text_mode_flush();
}

static void on_search_confirm(ui_text_input_widget_t *widget, void *user_data) {
    (void)widget;
    if (!user_data) {
        return;
    }
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;

    // Perform the search
    reader_nav_search_forward(state, state->search_buf, &bold_pending, &underline_pending);
}

static void on_search_cancel(ui_text_input_widget_t *widget, void *user_data) {
    (void)widget;
    if (!user_data) {
        return;
    }
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;

    // Return to reading mode
    state->mode = MODE_READING;
    reader_view_draw_reading_page(state, &bold_pending, &underline_pending);
    text_mode_flush();
}

void reader_nav_start_search(reader_state_t *state) {
    state->mode = MODE_SEARCH;

    // Clear previous search buffer
    state->search_buf[0] = '\0';

    // Create search widget if it doesn't exist
    if (!state->search_widget) {
        int cols = text_mode_get_cols();
        int rows = text_mode_get_rows();

        state->search_widget = ui_text_input_create(0, rows - 4, cols, 4);
        ui_text_input_set_title(state->search_widget, "Search Forward");
        ui_text_input_set_label(state->search_widget, "Text:");
        ui_text_input_set_hints(state->search_widget, "Type text  Enter Search", "ESC Cancel");
        ui_text_input_set_callbacks(state->search_widget, NULL, on_search_confirm, on_search_cancel, state);
    }

    // Set buffer and redraw
    ui_text_input_set_buffer(state->search_widget, state->search_buf, sizeof(state->search_buf));
    ui_text_input_draw(state->search_widget);
    text_mode_flush();
}

static void reader_nav_search_forward(reader_state_t *state, const char *query, int *bold_pending, int *underline_pending) {
    if (!query || !query[0] || !state->file) {
        snprintf(state->search_status, sizeof(state->search_status), "Search text is empty");
        reader_view_draw_reading_page(state, bold_pending, underline_pending);
        return;
    }

    uint32_t start_offset = page_cache_current_offset(&state->page_cache);
    int start_page = state->page_number;

    md_clear_remainder();
    // Start from the NEXT page to avoid finding the same result again
    fseek(state->file, start_offset, SEEK_SET);
    // Skip the current page by advancing past it
    md_scan_page(state->file, state->lines, state->content_rows, state->screen_width);

    int page = start_page + 1;  // Start from next page
    while (1) {
        uint32_t page_offset = (uint32_t)ftell(state->file);
        int line_count = md_scan_page(state->file, state->lines, state->content_rows, state->screen_width);
        if (line_count == 0) {
            break;
        }

        state->line_count = line_count;
        if (page_contains_query(state, query)) {
            page_cache_init(&state->page_cache);
            page_cache_set_start(&state->page_cache, page_offset);
            state->page_number = page;
            md_clear_remainder();
            reader_load_current_page(state, bold_pending, underline_pending);
            snprintf(state->search_status, sizeof(state->search_status), "Found \"%s\" on page %d", query, page);
            reader_view_draw_reading_page(state, bold_pending, underline_pending);
            return;
        }

        page++;
    }

    md_clear_remainder();
    fseek(state->file, start_offset, SEEK_SET);
    state->page_number = start_page;
    reader_load_current_page(state, bold_pending, underline_pending);
    snprintf(state->search_status, sizeof(state->search_status), "Not found: \"%s\"", query);
    reader_view_draw_reading_page(state, bold_pending, underline_pending);
}

void reader_nav_handle_search_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (!state->search_widget) {
        return;
    }

    // Let the widget handle the key
    if (ui_text_input_handle_key(state->search_widget, key)) {
        // Widget handled the key, only redraw if still in search mode
        if (state->mode == MODE_SEARCH) {
            ui_text_input_draw(state->search_widget);
            text_mode_flush();
        }
    }
    // Callbacks handle mode switching and search execution
}
