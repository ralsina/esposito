#include "reader_events.h"

#include "app_config.h"
#include "reader_core.h"
#include "reader_md.h"
#include "reader_nav.h"
#include "reader_toc.h"
#include "reader_view.h"
#include "text_mode.h"
#include "ui_button.h"

#include <string.h>

#define TOUCH_PAGE_SPLIT_X 160
#define READING_TOC_BUTTON_WIDTH 5
#define READING_BACK_BUTTON_WIDTH 5

static int load_current_page(reader_state_t *state, int *bold_pending, int *underline_pending) {
    return reader_load_current_page(state, bold_pending, underline_pending);
}

// Forward declarations for static functions
static void toc_return_to_reading(reader_state_t *state, int *bold_pending, int *underline_pending);
static void toc_move_selection(reader_state_t *state, int delta);
static void toc_jump_to_selected(reader_state_t *state, int *bold_pending, int *underline_pending);
static void change_file_selection(reader_state_t *state, int delta);
static void open_selected_book(reader_state_t *state, int *bold_pending, int *underline_pending);
static void exit_to_app_list(int *bold_pending, int *underline_pending, void (*launch_app_list)(void));
static void enter_toc_mode(reader_state_t *state);
static void exit_to_file_list(reader_state_t *state);

// Button widget callbacks
void on_file_list_up_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    change_file_selection(state, -1);
}

void on_file_list_open_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;
    open_selected_book(state, &bold_pending, &underline_pending);
}

void on_file_list_down_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    change_file_selection(state, 1);
}

void on_file_list_exit_click(ui_button_t *button, void *user_data) {
    // This needs special handling - we'll set a flag and handle it in the main loop
    // For now, do nothing - the file list exit is handled via ESC key
}

void on_toc_up_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;
    toc_move_selection(state, -1);
}

void on_toc_jump_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;
    toc_jump_to_selected(state, &bold_pending, &underline_pending);
}

void on_toc_down_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;
    toc_move_selection(state, 1);
}

void on_toc_back_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    int bold_pending = 0, underline_pending = 0;
    toc_return_to_reading(state, &bold_pending, &underline_pending);
}

void on_reading_toc_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    enter_toc_mode(state);
}

void on_reading_back_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    exit_to_file_list(state);
}

// TOC list widget callbacks
void on_toc_list_selection_changed(ui_list_widget_t *list, int new_selection) {
    if (!list) {
        return;
    }
    reader_state_t *state = (reader_state_t*)list->user_data;
    if (!state) {
        return;
    }
    state->toc_selected = new_selection;
}

void on_toc_list_item_selected(ui_list_widget_t *list, int item_index) {
    if (!list) {
        return;
    }
    reader_state_t *state = (reader_state_t*)list->user_data;
    if (!state) {
        return;
    }
    int bold_pending = 0, underline_pending = 0;
    toc_jump_to_selected(state, &bold_pending, &underline_pending);
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
    reader_close_current_file(state);
    reader_events_show_file_list(state);
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

static void enter_toc_mode(reader_state_t *state) {
    if (state->toc_count == 0) {
        reader_toc_load_or_build(state);
    }

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
}

static void handle_reading_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (key == 'w' || key == 'W') {
        reader_nav_prev_page(state, bold_pending, underline_pending);
    } else if (key == 's' || key == 'S') {
        reader_nav_next_page(state, bold_pending, underline_pending);
    } else if (key == 'g' || key == 'G') {
        reader_nav_start_goto(state);
    } else if (key == '/') {
        reader_nav_start_search(state);
    } else if (key == 't' || key == 'T') {
        enter_toc_mode(state);
    } else if (key == 27) {
        exit_to_file_list(state);
    }
}

static void handle_file_list_touch(reader_state_t *state, int x_col, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    // This function is no longer needed - touch is handled by dispatch_touch using button widgets
    // Kept for compatibility but should not be called
}

static void handle_reading_touch(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending) {
    // Convert pixel coordinates to character coordinates for button widgets
    int x_col = event->touch.x / 5;   // 5 pixels per character column
    int y_col = event->touch.y / 8;   // 8 pixels per character row

    // Create a modified touch event with character coordinates
    event_t char_event = *event;
    char_event.touch.x = x_col;
    char_event.touch.y = y_col;

    // Check header buttons first (in row 0)
    if (y_col == 0) {
        if (state->btn_jump && ui_button_handle_touch(state->btn_jump, &char_event)) return;
        if (state->btn_back && ui_button_handle_touch(state->btn_back, &char_event)) return;
    }

    // Page navigation touch zones
    if (event->touch.x < TOUCH_PAGE_SPLIT_X) {
        reader_nav_prev_page(state, bold_pending, underline_pending);
    } else {
        reader_nav_next_page(state, bold_pending, underline_pending);
    }
}

static void toc_return_to_reading(reader_state_t *state, int *bold_pending, int *underline_pending) {
    state->mode = MODE_READING;
    reader_view_draw_reading_page(state, bold_pending, underline_pending);
    text_mode_flush();
}

static void toc_move_selection(reader_state_t *state, int delta) {
    int previous_selected = state->toc_selected;
    int next = state->toc_selected + delta;
    if (next < 0 || next >= state->toc_count) {
        return;
    }
    state->toc_selected = next;

    if (state->toc_count > 0) {
        reader_view_update_toc_selection(state, previous_selected);
    } else {
        reader_view_draw_toc(state);
    }

    text_mode_flush();
}

static void toc_jump_to_selected(reader_state_t *state, int *bold_pending, int *underline_pending) {
    if (!state || state->toc_count <= 0 || !state->toc) {
        return;
    }

    if (state->toc_selected < 0 || state->toc_selected >= state->toc_count) {
        return;
    }

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

static void handle_toc_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    if (key == 27) {
        toc_return_to_reading(state, bold_pending, underline_pending);
        return;
    }

    // Try list widget first for navigation keys
    if (state->toc_list && ui_list_handle_key(state->toc_list, key)) {
        // Check if we switched to reading mode (Enter was pressed)
        if (state->mode == MODE_READING) {
            return; // Don't redraw TOC, we're now in reading mode
        }
        // List widget handled the key, redraw the updated list
        ui_list_draw(state->toc_list);
        text_mode_flush();
        return;
    }

    // Fall back to button handling for other keys if needed
}

static void handle_toc_touch(reader_state_t *state, int x_col, int *bold_pending, int *underline_pending) {
    if (x_col >= state->btn_up_x && x_col < state->btn_up_x + state->btn_w) {
        toc_move_selection(state, -1);
    } else if (x_col >= state->btn_open_x && x_col < state->btn_open_x + state->btn_w) {
        toc_jump_to_selected(state, bold_pending, underline_pending);
    } else if (x_col >= state->btn_down_x && x_col < state->btn_down_x + state->btn_w) {
        toc_move_selection(state, 1);
    } else if (x_col >= state->btn_exit_x && x_col < state->btn_exit_x + state->btn_w) {
        toc_return_to_reading(state, bold_pending, underline_pending);
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
        case MODE_SEARCH:
            reader_nav_handle_search_key(state, key, bold_pending, underline_pending);
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

    if (state->mode == MODE_TOC || state->mode == MODE_FILE_LIST) {
        // Convert pixel coordinates to character coordinates for widgets
        // Screen is 320x240 pixels, text grid is 64x30 characters
        // Character size: 320/64 = 5 pixels wide, 240/30 = 8 pixels tall
        int x_col = event->touch.x / 5;   // 5 pixels per character column
        int y_col = event->touch.y / 8;   // 8 pixels per character row

        // Create a modified touch event with character coordinates
        event_t char_event = *event;
        char_event.touch.x = x_col;
        char_event.touch.y = y_col;

        // Try list widget first for TOC mode
        if (state->mode == MODE_TOC && state->toc_list &&
            ui_list_handle_touch(state->toc_list, &char_event)) {
            return; // List widget handled the touch
        }

        // Check exit button separately for file list mode (needs launch_app_list)
        if (state->mode == MODE_FILE_LIST && state->btn_exit &&
            ui_button_handle_touch(state->btn_exit, &char_event)) {
            // Exit button was pressed - launch app list
            reader_close_current_file(state);
            config_set_string(KEY_LAST_FILE, "");
            if (launch_app_list) {
                launch_app_list();
            }
            return;
        }

        // Try button widgets with character coordinates
        if (state->btn_up && ui_button_handle_touch(state->btn_up, &char_event)) return;
        if (state->btn_open && ui_button_handle_touch(state->btn_open, &char_event)) return;
        if (state->btn_down && ui_button_handle_touch(state->btn_down, &char_event)) return;
        if (state->mode == MODE_TOC && state->btn_exit && ui_button_handle_touch(state->btn_exit, &char_event)) return;
        return;
    }
}

void reader_events_handle_event(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        dispatch_keyboard(state, event, bold_pending, underline_pending);
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        dispatch_touch(state, event, bold_pending, underline_pending, launch_app_list);
    }
}
