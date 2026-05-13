#include "os_core.h"
#include "checkpoint.h"
#include "text_mode.h"
#include "reader_md.h"
#include "reader_state.h"
#include "reader_core.h"
#include "reader_nav.h"
#include "reader_view.h"
#include "ui.h"
extern void app_launcher_start(void);
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#define TOUCH_PAGE_SPLIT_X 160

#define READING_CLOSE_BUTTON_WIDTH 4

static reader_state_t state;
static int bold_pending = 0;
static int underline_pending = 0;

static int load_current_page(void);
static int open_book(const char *path);

static void enter_reading_mode(void) {
    state.mode = MODE_READING;
    state.screen_width = text_mode_get_cols() - MARGIN * 2;
    state.content_rows = text_mode_get_rows() - 2;
    load_current_page();
    reader_view_draw_reading_page(&state, &bold_pending, &underline_pending);
}

static void change_file_selection(int delta) {
    int next = state.file_selected + delta;
    if (next < 0 || next >= state.file_count) {
        return;
    }
    state.file_selected = next;
    reader_view_draw_file_list(&state);
}

static void open_selected_book(void) {
    if (state.file_count <= 0) {
        return;
    }
    if (open_book(state.file_paths[state.file_selected])) {
        enter_reading_mode();
    }
}

static int open_book(const char *path) {
    md_clear_remainder();
    bold_pending = 0;
    underline_pending = 0;
    return reader_open_file(&state, path);
}

static int load_current_page(void) {
    return reader_load_current_page(&state, &bold_pending, &underline_pending);
}

static void exit_to_file_list(void) {
    char last_path[MAX_PATH];
    strncpy(last_path, state.current_file, sizeof(last_path) - 1);
    last_path[sizeof(last_path) - 1] = '\0';

    state.mode = MODE_FILE_LIST;
    reader_close_current_file(&state);
    reader_scan_md_files(&state);
    int selected_index = reader_find_file_index_by_path(&state, last_path);
    state.file_selected = (selected_index >= 0) ? selected_index : 0;
    checkpoint_save_string(KEY_LAST_FILE, "");
    reader_view_draw_file_list(&state);
}

static void handle_file_list_event(char key) {
    if (key == 'w' || key == 'W') {
        change_file_selection(-1);
    } else if (key == 's' || key == 'S') {
        change_file_selection(1);
    } else if (key == '\n' || key == '\r') {
        open_selected_book();
    }
}

static void handle_reading_event(char key) {
    if (key == 'w' || key == 'W') {
        reader_nav_prev_page(&state, &bold_pending, &underline_pending);
    } else if (key == 's' || key == 'S') {
        reader_nav_next_page(&state, &bold_pending, &underline_pending);
    } else if (key == 'g' || key == 'G') {
        reader_nav_start_goto(&state);
    } else if (key == 27) {
        exit_to_file_list();
    }
}

static void handle_goto_event(char key) {
    reader_nav_handle_goto_key(&state, key, &bold_pending, &underline_pending);
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    ctx->timer_interval_ms = 0;

    memset(&state, 0, sizeof(state));
    text_mode_init();
    checkpoint_open("reader");

    // Try last-opened file restore (legacy fallback included).
    const char *saved_file = checkpoint_load_string(KEY_LAST_FILE);
    if (!saved_file || !saved_file[0]) {
        saved_file = checkpoint_load_string(KEY_LEGACY_LAST_FILE);
    }

    if (saved_file && saved_file[0]) {
        struct stat st;
        if (stat(saved_file, &st) == 0 && S_ISREG(st.st_mode)) {
            if (open_book(saved_file)) {
                enter_reading_mode();
                return;
            }
        }
    }

    // Show file list
    reader_scan_md_files(&state);
    state.file_selected = 0;
    state.mode = MODE_FILE_LIST;
    reader_view_draw_file_list(&state);
}

static void handle_file_list_touch(int x_col) {
    if (x_col >= state.btn_up_x && x_col < state.btn_up_x + state.btn_w) {
        change_file_selection(-1);
    } else if (x_col >= state.btn_open_x && x_col < state.btn_open_x + state.btn_w) {
        open_selected_book();
    } else if (x_col >= state.btn_down_x && x_col < state.btn_down_x + state.btn_w) {
        change_file_selection(1);
    } else if (x_col >= state.btn_exit_x && x_col < state.btn_exit_x + state.btn_w) {
        reader_close_current_file(&state);
        checkpoint_save_string(KEY_LAST_FILE, "");
        app_launcher_start();
    }
}

static void handle_reading_touch(const event_t *event) {
    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();
    int cols = text_mode_get_cols();

    if (event->touch.x >= (cols - READING_CLOSE_BUTTON_WIDTH) * char_width && event->touch.y < char_height * 2) {
        exit_to_file_list();
    } else if (event->touch.x < TOUCH_PAGE_SPLIT_X) {
        reader_nav_prev_page(&state, &bold_pending, &underline_pending);
    } else {
        reader_nav_next_page(&state, &bold_pending, &underline_pending);
    }
}

static char normalize_key_for_dispatch(const event_t *event) {
    char key = event->keyboard.key;

    // If keyboard firmware maps Ctrl+letter to control chars, map back to letters
    // so navigation bindings remain robust.
    if ((event->keyboard.modifiers & MODIFIER_CTRL) && key >= 1 && key <= 26) {
        key = (char)('a' + key - 1);
    }

    return key;
}

static void dispatch_keyboard(const event_t *event) {
    char key = normalize_key_for_dispatch(event);

    switch (state.mode) {
        case MODE_FILE_LIST:
            handle_file_list_event(key);
            break;
        case MODE_GOTO:
            handle_goto_event(key);
            break;
        case MODE_READING:
            handle_reading_event(key);
            break;
    }
}

static void dispatch_touch(const event_t *event) {
    if (state.mode == MODE_READING) {
        handle_reading_touch(event);
        return;
    }

    if (state.mode != MODE_FILE_LIST) {
        return;
    }

    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();
    if (event->touch.y < state.btn_row * char_height || event->touch.y >= (state.btn_row + 1) * char_height) {
        return;
    }

    int x_col = event->touch.x / char_width;
    handle_file_list_touch(x_col);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        dispatch_keyboard(event);
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        dispatch_touch(event);
    }
}

void app_checkpoint(app_context_t *ctx) {
    reader_save_current_book_progress(&state);
}

void app_close(app_context_t *ctx) {
    reader_close_current_file(&state);
    checkpoint_close();
    text_mode_clear(TEXT_COLOR_BLACK);
}
