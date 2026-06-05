#include "reader_events.h"

#include "app_config.h"
#include "reader_core.h"
#include "reader_nav.h"
#include "reader_toc.h"
#include "reader_view.h"
#include "text_mode.h"
#include "ui_button.h"
#include "hardware.h"
#include "serial_rx.h"  // For file transfer

#include <string.h>
#include <sys/stat.h>

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

// Receiving mode forward declarations
static bool on_get_file_start(const char *filename, size_t size, char *out_filepath);
static void on_get_progress(size_t received, size_t total, uint16_t seq, const char *status);
static void on_get_complete(serial_rx_state_t state, const char *filename, const char *error_msg);
static void cancel_receiving(reader_state_t *state);
static reader_state_t *receiving_state = NULL;

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
    reader_state_t *state = (reader_state_t*)user_data;
    reader_close_current_file(state);
    config_set_string(KEY_LAST_FILE, "");
    if (state->launch_app_list) {
        state->launch_app_list();
    }
}

void on_file_list_get_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;

    serial_log_output_set_enabled(false);
    serial_init(115200, 8, 'N', 1);

    receiving_state = state;

    serial_rx_config_t config = {
        .on_file_start = on_get_file_start,
        .on_progress = on_get_progress,
        .on_complete = on_get_complete,
    };
    serial_rx_init(&config);

    state->mode = MODE_RECEIVING;
    reader_view_draw_receiving(state);
    text_mode_flush();
}

static void on_get_progress(size_t received, size_t total, uint16_t seq, const char *status) {
    (void)seq;
    (void)status;
    reader_state_t *rs = receiving_state;
    if (!rs) return;
    // Update every 8 packets to avoid slowing down the transfer
    if (seq % 8 != 0 && received < total) return;
    reader_view_update_progress(rs, received, total);
}

static bool on_get_file_start(const char *filename, size_t size, char *out_filepath) {
    snprintf(out_filepath, 256, "/sdcard/downloads/%s", filename);
    mkdir("/sdcard/downloads", 0777);
    reader_state_t *rs = receiving_state;
    if (rs && filename) {
        strncpy(rs->receiving_filename, filename, sizeof(rs->receiving_filename) - 1);
        rs->receiving_filename[sizeof(rs->receiving_filename) - 1] = '\0';
        reader_view_draw_receiving(rs);
        text_mode_flush();
    }
    return true;
}

static void on_get_complete(serial_rx_state_t state, const char *filename, const char *error_msg) {
    reader_state_t *rs = receiving_state;
    if (!rs) return;

    if (state == SERIAL_RX_STATE_SUCCESS) {
        const char *fp = serial_rx_get_filepath();
        if (fp && fp[0]) {
            size_t len = strlen(fp);
            if (len >= 3 && strcmp(fp + len - 3, ".md") == 0) {
                char book_path[256];
                snprintf(book_path, sizeof(book_path), "/sdcard/books/%s", filename);
                mkdir("/sdcard/books", 0777);
                if (rename(fp, book_path) == 0) {
                    reader_scan_md_files(rs);
                    int idx = reader_find_file_index_by_path(rs, book_path);
                    if (idx >= 0) {
                        rs->file_selected = idx;
                    }
                } else {
                    reader_scan_md_files(rs);
                }
            } else {
                reader_scan_md_files(rs);
            }
        }
    }

    serial_rx_reset();
    serial_log_output_set_enabled(true);
    receiving_state = NULL;

    rs->ignore_events = 1;
    rs->mode = MODE_FILE_LIST;
    reader_view_draw_file_list(rs);
    text_mode_flush();
}

void on_cancel_click(ui_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t*)user_data;
    cancel_receiving(state);
}

static void cancel_receiving(reader_state_t *state) {
    serial_rx_reset();
    serial_log_output_set_enabled(true);
    receiving_state = NULL;
    state->ignore_events = 1;
    state->mode = MODE_FILE_LIST;
    reader_view_draw_file_list(state);
    text_mode_flush();
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

void on_reading_find_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    reader_nav_start_search(state);
}

void on_reading_goto_click(ui_button_t *button, void *user_data) {
    reader_state_t *state = (reader_state_t*)user_data;
    reader_nav_start_goto(state);
}

// TOC list widget callbacks
void on_toc_list_selection_changed(ui_list_widget_t *list, int new_selection, void *user_data) {
    (void)list;
    reader_state_t *state = (reader_state_t*)user_data;
    if (!state) {
        return;
    }
    state->toc_selected = new_selection;
}

void on_toc_list_item_selected(ui_list_widget_t *list, int item_index, void *user_data) {
    (void)list;
    (void)item_index;
    reader_state_t *state = (reader_state_t*)user_data;
    if (!state) {
        return;
    }
    int bold_pending = 0, underline_pending = 0;
    toc_jump_to_selected(state, &bold_pending, &underline_pending);
}

// File list widget callbacks
void on_file_list_selection_changed(ui_list_widget_t *list, int new_selection, void *user_data) {
    (void)list;
    reader_state_t *state = (reader_state_t*)user_data;
    if (!state) {
        return;
    }
    state->file_selected = new_selection;
}

void on_file_list_item_selected(ui_list_widget_t *list, int item_index, void *user_data) {
    (void)list;
    reader_state_t *state = (reader_state_t*)user_data;
    if (!state || item_index < 0 || item_index >= state->file_count) {
        return;
    }
    state->file_selected = item_index;
    int bold_pending = 0, underline_pending = 0;
    open_selected_book(state, &bold_pending, &underline_pending);
}

int reader_events_open_book(reader_state_t *state, const char *path, int *bold_pending, int *underline_pending) {
    *bold_pending = 0;
    *underline_pending = 0;
    return reader_open_file(state, path);
}

void reader_events_enter_reading_mode(reader_state_t *state, int *bold_pending, int *underline_pending) {
    state->mode = MODE_READING;
    state->screen_width = text_mode_get_cols() - MARGIN * 2;
    // Calculate content rows based on orientation
    int rows = text_mode_get_rows();
    bool is_portrait = display_get_height() >= display_get_width();
    if (is_portrait) {
        state->content_rows = rows - 4;  // Account for top 2 rows and bottom 2 rows in portrait
    } else {
        state->content_rows = rows - 2;  // Account for top 2 rows in landscape
    }

    // Only initialize page cache if it's empty or screen dimensions changed
    if (state->page_cache.count == 0) {
        uint32_t current_offset = ftell(state->file);
        page_cache_init(&state->page_cache);
        state->page_cache.entries[0].file_pos = current_offset;
        state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
        state->page_cache.count = 1;
        state->page_cache.current = 0;
        state->page_cache.entries[0].screen_width = state->screen_width;
        state->page_cache.entries[0].content_rows = state->content_rows;
    }

    // If restored from a saved offset (file_pos > 0), compute the page number
    // by scanning from the beginning. Page number depends on font/screen size and
    // may differ from the previous session.
    if (state->page_cache.entries[0].file_pos > 0 && state->page_number == 1) {
        state->page_number = reader_compute_page_number(state);
    }

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
    char prev_file[MAX_PATH];
    strncpy(prev_file, state->current_file, MAX_PATH);
    prev_file[MAX_PATH - 1] = '\0';
    reader_close_current_file(state);
    reader_scan_md_files(state);
    state->file_selected = 0;
    if (prev_file[0]) {
        for (int i = 0; i < state->file_count; i++) {
            if (strcmp(state->file_paths[i], prev_file) == 0) {
                state->file_selected = i;
                break;
            }
        }
    }
    state->mode = MODE_FILE_LIST;
    reader_view_draw_file_list(state);
}

static void handle_file_list_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    // Try list widget first for navigation keys
    if (state->file_list && ui_list_handle_key(state->file_list, key)) {
        // Check if we switched to reading mode (Enter was pressed)
        if (state->mode == MODE_READING) {
            return; // Don't redraw file list, we're now in reading mode
        }
        // List widget handled the key, redraw the updated list
        ui_list_draw(state->file_list);
        text_mode_flush();
        return;
    }

    // ESC key handling is done in dispatch_keyboard
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
    // Check buttons first - they will handle their own bounds checking
    if (state->btn_jump && ui_button_handle_touch(state->btn_jump, event)) return;
    if (state->btn_find && ui_button_handle_touch(state->btn_find, event)) return;
    if (state->btn_goto && ui_button_handle_touch(state->btn_goto, event)) return;
    if (state->btn_back && ui_button_handle_touch(state->btn_back, event)) return;

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
    fseek(state->file, entry->file_offset, SEEK_SET);
    page_cache_init(&state->page_cache);
    state->page_cache.entries[0].file_pos = entry->file_offset;
    state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
    state->page_cache.count = 1;
    state->page_cache.current = 0;
    state->page_number = entry->page_number;
    reader_load_current_page(state, bold_pending, underline_pending);
    reader_view_draw_reading_page(state, bold_pending, underline_pending);
    reader_save_current_book_progress(state);
    text_mode_flush();
}

static void handle_receiving_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending) {
    (void)bold_pending;
    (void)underline_pending;
    if (key == 27) {
        cancel_receiving(state);
    }
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


static char normalize_key_for_dispatch(const event_t *event) {
    char key = event->keyboard.key;

    if ((event->keyboard.modifiers & MODIFIER_CTRL) && key >= 1 && key <= 26) {
        key = (char)('a' + key - 1);
    }

    return key;
}

static void dispatch_keyboard(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending) {
    // Check if OSK is active and handle its events first
    if (ui_osk_is_active()) {
        ui_osk_handle_event(NULL, (event_t*)event);

        // Check if OSK just completed
        if (!ui_osk_is_active()) {
            ui_osk_result_t result = ui_osk_get_result();

            if (result == UI_OSK_RESULT_CONFIRMED) {
                // OSK completed successfully - handle the result based on current mode
                if (state->mode == MODE_GOTO) {
                    on_goto_confirm(NULL, state);
                } else if (state->mode == MODE_SEARCH) {
                    on_search_confirm(NULL, state);
                }

                // Ensure we return to reading mode after confirmation
                state->mode = MODE_READING;
                reader_view_draw_reading_page(state, bold_pending, underline_pending);
                text_mode_flush();
            } else {
                // OSK was cancelled - return to reading mode
                state->mode = MODE_READING;
                reader_view_draw_reading_page(state, bold_pending, underline_pending);
                text_mode_flush();
            }
        }
        return;
    }

    if (state->ignore_events > 0) {
        state->ignore_events--;
        return;
    }

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
        case MODE_RECEIVING:
            handle_receiving_key(state, key, bold_pending, underline_pending);
            break;
    }
}

static void dispatch_touch(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    // Check if OSK is active and handle its touch events first
    if (ui_osk_is_active()) {
        ui_osk_handle_event(NULL, (event_t*)event);

        // Check if OSK just completed
        if (!ui_osk_is_active()) {
            ui_osk_result_t result = ui_osk_get_result();

            if (result == UI_OSK_RESULT_CONFIRMED) {
                // OSK completed successfully - handle the result based on current mode
                if (state->mode == MODE_GOTO) {
                    on_goto_confirm(NULL, state);
                } else if (state->mode == MODE_SEARCH) {
                    on_search_confirm(NULL, state);
                }

                // Ensure we return to reading mode after confirmation
                state->mode = MODE_READING;
                reader_view_draw_reading_page(state, bold_pending, underline_pending);
                text_mode_flush();
            } else {
                // OSK was cancelled - return to reading mode
                state->mode = MODE_READING;
                reader_view_draw_reading_page(state, bold_pending, underline_pending);
                text_mode_flush();
            }
        }
        return;
    }

    if (state->mode == MODE_RECEIVING) {
        if (state->btn_cancel && ui_button_handle_touch(state->btn_cancel, event)) {
            return;
        }
        return;
    }

    if (state->mode == MODE_READING) {
        handle_reading_touch(state, event, bold_pending, underline_pending);
        return;
    }

    if (state->mode == MODE_TOC || state->mode == MODE_FILE_LIST) {
        // UI widgets handle pixel-to-character conversion internally
        // Pass the original pixel coordinates directly

        // Try list widget first for TOC mode
        if (state->mode == MODE_TOC && state->toc_list &&
            ui_list_handle_touch(state->toc_list, event)) {
            return; // List widget handled the touch
        }

        if (state->mode == MODE_FILE_LIST) {
            // Try toolbar buttons
            if (state->file_list_toolbar && ui_toolbar_handle_touch(state->file_list_toolbar, event)) {
                return;
            }

            // Try list widget
            if (state->file_list && ui_list_handle_touch(state->file_list, event)) {
                if (state->mode == MODE_READING) {
                    return;
                }
                ui_list_draw(state->file_list);
                text_mode_flush();
                return;
            }
            return;
        }

        // TOC mode button handling
        if (state->mode == MODE_TOC) {
            if (state->btn_up && ui_button_handle_touch(state->btn_up, event)) return;
            if (state->btn_open && ui_button_handle_touch(state->btn_open, event)) return;
            if (state->btn_down && ui_button_handle_touch(state->btn_down, event)) return;
            if (state->btn_exit && ui_button_handle_touch(state->btn_exit, event)) return;
        }
        return;
    }
}

void reader_events_handle_event(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    state->launch_app_list = launch_app_list;

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        dispatch_keyboard(state, event, bold_pending, underline_pending);
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        dispatch_touch(state, event, bold_pending, underline_pending, launch_app_list);
    }
}
