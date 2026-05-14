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
    
    state->goto_widget.title = "Go to Page";
    state->goto_widget.label = "Page:";
    state->goto_widget.buffer = state->goto_buf;
    state->goto_widget.max_len = sizeof(state->goto_buf);
    state->goto_widget.mask_input = false;
    state->goto_widget.hint_left = "Type number  Enter Confirm";
    state->goto_widget.hint_right = "ESC Cancel";
    
    ui_text_input_widget_draw(&state->goto_widget);
    text_mode_flush();
}

void reader_nav_handle_goto_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    // Create a minimal event for the widget handler
    event_t event = {
        .type = EVENT_KEYBOARD,
        .keyboard = {
            .key = key,
            .pressed = 1,
            .modifiers = 0
        }
    };
    
    int result = ui_text_input_widget_handle_event(&state->goto_widget, &event);
    
    if (result == 1) {
        // Enter confirmed
        int page = atoi(state->goto_buf);
        state->mode = MODE_READING;
        reader_nav_goto_page(state, (page > 1) ? page : 1, bold_pending, underline_pending);
    } else if (result == -1) {
        // ESC cancelled
        state->mode = MODE_READING;
        reader_view_draw_reading_page(state, bold_pending, underline_pending);
    }
    // result == 0 means still editing, widget handles redraw
}

void reader_nav_start_search(reader_state_t *state) {
    state->mode = MODE_SEARCH;

    state->search_widget.title = "Search Forward";
    state->search_widget.label = "Text:";
    state->search_widget.buffer = state->search_buf;
    state->search_widget.max_len = sizeof(state->search_buf);
    state->search_widget.mask_input = false;
    state->search_widget.hint_left = "Type text  Enter Search";
    state->search_widget.hint_right = "ESC Cancel";

    ui_text_input_widget_draw(&state->search_widget);
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
    fseek(state->file, start_offset, SEEK_SET);

    int page = start_page;
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
    event_t event = {
        .type = EVENT_KEYBOARD,
        .keyboard = {
            .key = key,
            .pressed = 1,
            .modifiers = 0
        }
    };

    int result = ui_text_input_widget_handle_event(&state->search_widget, &event);
    if (result == 1) {
        state->mode = MODE_READING;
        reader_nav_search_forward(state, state->search_buf, bold_pending, underline_pending);
    } else if (result == -1) {
        state->mode = MODE_READING;
        reader_view_draw_reading_page(state, bold_pending, underline_pending);
    }
}
