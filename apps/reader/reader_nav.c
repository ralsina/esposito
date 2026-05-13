#include "reader_nav.h"

#include "reader_core.h"
#include "reader_md.h"
#include "reader_view.h"
#include "text_mode.h"

#include <stdio.h>
#include <stdlib.h>

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
    state->goto_pos = 0;
    state->goto_buf[0] = '\0';
    reader_view_draw_goto_prompt(state);
}

void reader_nav_handle_goto_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (key >= '0' && key <= '9') {
        if (state->goto_pos < (int)sizeof(state->goto_buf) - 1) {
            state->goto_buf[state->goto_pos++] = key;
            state->goto_buf[state->goto_pos] = '\0';
            reader_view_draw_goto_prompt(state);
        }
        return;
    }

    if (key == '\n' || key == '\r') {
        int page = atoi(state->goto_buf);
        state->mode = MODE_READING;
        reader_nav_goto_page(state, (page > 1) ? page : 1, bold_pending, underline_pending);
        return;
    }

    if (key == 27) {
        state->mode = MODE_READING;
        reader_view_draw_reading_page(state, bold_pending, underline_pending);
    }
}
