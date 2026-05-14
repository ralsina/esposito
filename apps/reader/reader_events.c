#include "reader_events.h"

#include "checkpoint.h"
#include "reader_core.h"
#include "reader_md.h"
#include "reader_nav.h"
#include "reader_toc.h"
#include "reader_view.h"
#include "text_mode.h"

#include <string.h>

#define TOUCH_PAGE_SPLIT_X 160
#define READING_CLOSE_BUTTON_WIDTH 4

static int load_current_page(reader_state_t *state, int *bold_pending, int *underline_pending) {
    return reader_load_current_page(state, bold_pending, underline_pending);
}

int reader_events_open_book(reader_state_t *state, const char *path, int *bold_pending, int *underline_pending) {
    md_clear_remainder();
    *bold_pending = 0;
    *underline_pending = 0;
    return reader_open_file(state, path);
}

void reader_events_enter_reading_mode(reader_state_t *state, int *bold_pending, int *underline_pending) {
    state->mode = MODE_READING;
    state->screen_width = text_mode_get_cols() - MARGIN * 2;
    state->content_rows = text_mode_get_rows() - 2;
    load_current_page(state, bold_pending, underline_pending);
    reader_view_draw_reading_page(state, bold_pending, underline_pending);
}

void reader_events_show_file_list(reader_state_t *state) {
    reader_scan_md_files(state);
    state->file_selected = 0;
    state->mode = MODE_FILE_LIST;
    reader_view_draw_file_list(state);
}

static void change_file_selection(reader_state_t *state, int delta) {
    int next = state->file_selected + delta;
    if (next < 0 || next >= state->file_count) {
        return;
    }
    state->file_selected = next;
    reader_view_draw_file_list(state);
}

static void open_selected_book(reader_state_t *state, int *bold_pending, int *underline_pending) {
    if (state->file_count <= 0) {
        return;
    }
    if (reader_events_open_book(state, state->file_paths[state->file_selected], bold_pending, underline_pending)) {
        reader_events_enter_reading_mode(state, bold_pending, underline_pending);
    }
}

static void exit_to_file_list(reader_state_t *state) {
    char last_path[MAX_PATH];
    strncpy(last_path, state->current_file, sizeof(last_path) - 1);
    last_path[sizeof(last_path) - 1] = '\0';

    state->mode = MODE_FILE_LIST;
    reader_close_current_file(state);
    reader_scan_md_files(state);
    int selected_index = reader_find_file_index_by_path(state, last_path);
    state->file_selected = (selected_index >= 0) ? selected_index : 0;
    checkpoint_save_string(KEY_LAST_FILE, "");
    reader_view_draw_file_list(state);
}

static void handle_file_list_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (key == 'w' || key == 'W') {
        change_file_selection(state, -1);
    } else if (key == 's' || key == 'S') {
        change_file_selection(state, 1);
    } else if (key == '\n' || key == '\r') {
        open_selected_book(state, bold_pending, underline_pending);
    }
}

static void handle_reading_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (key == 'w' || key == 'W') {
        reader_nav_prev_page(state, bold_pending, underline_pending);
    } else if (key == 's' || key == 'S') {
        reader_nav_next_page(state, bold_pending, underline_pending);
    } else if (key == 'g' || key == 'G') {
        reader_nav_start_goto(state);
    } else if (key == 't' || key == 'T') {
        // Pre-select the closest TOC entry to current page
        state->toc_selected = 0;
        for (int i = 0; i < state->toc_count; i++) {
            if (state->toc[i].page_number <= state->page_number) {
                state->toc_selected = i;
            }
        }
        state->mode = MODE_TOC;
        reader_view_draw_toc(state);
        text_mode_flush();
    } else if (key == 27) {
        exit_to_file_list(state);
    }
}

static void handle_file_list_touch(reader_state_t *state, int x_col, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    if (x_col >= state->btn_up_x && x_col < state->btn_up_x + state->btn_w) {
        change_file_selection(state, -1);
    } else if (x_col >= state->btn_open_x && x_col < state->btn_open_x + state->btn_w) {
        open_selected_book(state, bold_pending, underline_pending);
    } else if (x_col >= state->btn_down_x && x_col < state->btn_down_x + state->btn_w) {
        change_file_selection(state, 1);
    } else if (x_col >= state->btn_exit_x && x_col < state->btn_exit_x + state->btn_w) {
        reader_close_current_file(state);
        checkpoint_save_string(KEY_LAST_FILE, "");
        launch_app_list();
    }
}

static void handle_reading_touch(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending) {
    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();
    int cols = text_mode_get_cols();

    if (event->touch.x >= (cols - READING_CLOSE_BUTTON_WIDTH) * char_width && event->touch.y < char_height * 2) {
        exit_to_file_list(state);
    } else if (event->touch.x < TOUCH_PAGE_SPLIT_X) {
        reader_nav_prev_page(state, bold_pending, underline_pending);
    } else {
        reader_nav_next_page(state, bold_pending, underline_pending);
    }
}

static void handle_toc_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (key == 27) {
        state->mode = MODE_READING;
        reader_view_draw_reading_page(state, bold_pending, underline_pending);
        text_mode_flush();
        return;
    }
    if (key == 'w' || key == 'W') {
        if (state->toc_selected > 0) {
            state->toc_selected--;
            reader_view_draw_toc(state);
            text_mode_flush();
        }
        return;
    }
    if (key == 's' || key == 'S') {
        if (state->toc_selected < state->toc_count - 1) {
            state->toc_selected++;
            reader_view_draw_toc(state);
            text_mode_flush();
        }
        return;
    }
    if (key == '\n' || key == '\r') {
        if (state->toc_count > 0) {
            const toc_entry_t *entry = &state->toc[state->toc_selected];
            state->mode = MODE_READING;
            md_clear_remainder();
            fseek(state->file, entry->file_offset, SEEK_SET);
            page_cache_init(&state->page_cache);
            page_cache_set_start(&state->page_cache, entry->file_offset);
            state->page_number = entry->page_number;
            reader_load_current_page(state, bold_pending, underline_pending);
            reader_view_draw_reading_page(state, bold_pending, underline_pending);
            text_mode_flush();
        }
        return;
    }
}

static char normalize_key_for_dispatch(const event_t *event) {
    char key = event->keyboard.key;

    if ((event->keyboard.modifiers & MODIFIER_CTRL) && key >= 1 && key <= 26) {
        key = (char)('a' + key - 1);
    }

    return key;
}

static void dispatch_keyboard(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending) {
    char key = normalize_key_for_dispatch(event);

    switch (state->mode) {
        case MODE_FILE_LIST:
            handle_file_list_key(state, key, bold_pending, underline_pending);
            break;
        case MODE_GOTO:
            reader_nav_handle_goto_key(state, key, bold_pending, underline_pending);
            break;
        case MODE_TOC:
            handle_toc_key(state, key, bold_pending, underline_pending);
            break;
        case MODE_READING:
            handle_reading_key(state, key, bold_pending, underline_pending);
            break;
    }
}

static void dispatch_touch(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    if (state->mode == MODE_READING) {
        handle_reading_touch(state, event, bold_pending, underline_pending);
        return;
    }

    if (state->mode != MODE_FILE_LIST) {
        return;
    }

    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();
    if (event->touch.y < state->btn_row * char_height || event->touch.y >= (state->btn_row + 1) * char_height) {
        return;
    }

    int x_col = event->touch.x / char_width;
    handle_file_list_touch(state, x_col, bold_pending, underline_pending, launch_app_list);
}

void reader_events_handle_event(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        dispatch_keyboard(state, event, bold_pending, underline_pending);
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        dispatch_touch(state, event, bold_pending, underline_pending, launch_app_list);
    }
}
