#include "reader_events.h"

#include "app_config.h"
#include "reader_core.h"
#include "reader_nav.h"
#include "reader_render_pipeline.h"
#include "reader_toc.h"
#include "reader_view.h"
#include "text_mode.h"
#include "ui2.h"
#include "hardware.h"
#include "serial_rx.h"
#include "ui2_osk.h"

#include <string.h>
#include <sys/stat.h>

#define TOUCH_PAGE_SPLIT_X 160

static reader_state_t *receiving_state = NULL;

static void handle_osk_result(reader_state_t *state, int *bold_pending, int *underline_pending) {
    ui2_osk_result_t result = ui2_osk_get_result();
    if (result == UI2_OSK_RESULT_CONFIRMED) {
        if (state->mode == MODE_GOTO) {
            on_goto_confirm(state);
        } else if (state->mode == MODE_SEARCH) {
            on_search_confirm(state);
        }
    }
    state->mode = MODE_READING;
    reader_view_setup_reading(state);
    reader_view_render_reading(state, bold_pending, underline_pending);
}

static void exit_to_file_list(reader_state_t *state) {
    render_pipeline_shutdown(&state->pipeline);
    state->lines = NULL;
    state->line_buf_size = 0;

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
}

static void enter_toc_mode(reader_state_t *state) {
    if (state->toc_count == 0) {
        reader_toc_load_or_build(state);
    }
    state->toc_selected = 0;
    for (int i = 0; i < state->toc_count; i++) {
        if (state->toc[i].page_number <= state->page_number) {
            state->toc_selected = i;
        }
    }
    state->mode = MODE_TOC;
}

static void cancel_receiving(reader_state_t *state) {
    serial_rx_reset();
    serial_log_output_set_enabled(true);
    receiving_state = NULL;
    state->ignore_events = 1;
    state->mode = MODE_FILE_LIST;
}

// Serial receive callbacks
static bool on_get_file_start(const char *filename, size_t size, char *out_filepath) {
    (void)size;
    snprintf(out_filepath, 256, "/sdcard/downloads/%s", filename);
    mkdir("/sdcard/downloads", 0777);
    reader_state_t *rs = receiving_state;
    if (rs && filename) {
        strncpy(rs->receiving_filename, filename, sizeof(rs->receiving_filename) - 1);
        rs->receiving_filename[sizeof(rs->receiving_filename) - 1] = '\0';
        reader_view_setup_receiving(rs);
        reader_view_render_receiving(rs);
    }
    return true;
}

static void on_get_progress(size_t received, size_t total, uint16_t seq, const char *status) {
    (void)seq;
    (void)status;
    reader_state_t *rs = receiving_state;
    if (!rs) return;
    if (seq % 8 != 0 && received < total) return;
    reader_view_update_progress(rs, received, total);
}

static void on_get_complete(serial_rx_state_t state, const char *filename, const char *error_msg) {
    (void)error_msg;
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
}

// Button callbacks
void on_file_list_up_click(ui2_button_t *button, void *user_data) {
    (void)button;
    ui2_list_t *list = (ui2_list_t *)user_data;
    if (list && list->count > 0 && list->selected > 0) {
        ui2_list_set_selection(list, list->selected - 1);
    }
}

void on_file_list_open_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    if (state->file_count <= 0) return;
    int bp = 0, up = 0;
    if (reader_events_open_book(state, state->file_paths[state->file_selected], &bp, &up)) {
        reader_events_enter_reading_mode(state, &bp, &up);
    }
}

void on_file_list_down_click(ui2_button_t *button, void *user_data) {
    (void)button;
    ui2_list_t *list = (ui2_list_t *)user_data;
    if (list && list->count > 0 && list->selected < list->count - 1) {
        ui2_list_set_selection(list, list->selected + 1);
    }
}

void on_file_list_exit_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    reader_close_current_file(state);
    config_set_string(KEY_LAST_FILE, "");
    if (state->launch_app_list) {
        state->launch_app_list();
    }
}

void on_file_list_get_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;

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
}

void on_cancel_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    cancel_receiving(state);
}

void on_toc_up_click(ui2_button_t *button, void *user_data) {
    (void)button;
    ui2_list_t *list = (ui2_list_t *)user_data;
    if (list && list->count > 0 && list->selected > 0) {
        ui2_list_set_selection(list, list->selected - 1);
    }
}

void on_toc_jump_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    int bp = 0, up = 0;
    if (!state || state->toc_count <= 0 || !state->toc) return;
    if (state->toc_selected < 0 || state->toc_selected >= state->toc_count) return;
    const toc_entry_t *entry = &state->toc[state->toc_selected];
    state->mode = MODE_READING;
    fseek(state->file, entry->file_offset, SEEK_SET);
    page_cache_init(&state->page_cache);
    state->page_cache.entries[0].file_pos = entry->file_offset;
    state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
    state->page_cache.count = 1;
    state->page_cache.current = 0;
    state->page_number = entry->page_number;
    reader_load_current_page(state, &bp, &up);
    reader_save_current_book_progress(state, false);
}

void on_toc_down_click(ui2_button_t *button, void *user_data) {
    (void)button;
    ui2_list_t *list = (ui2_list_t *)user_data;
    if (list && list->count > 0 && list->selected < list->count - 1) {
        ui2_list_set_selection(list, list->selected + 1);
    }
}

void on_toc_back_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    state->mode = MODE_READING;
}

void on_reading_toc_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    enter_toc_mode(state);
}

void on_reading_back_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    exit_to_file_list(state);
}

void on_reading_find_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    reader_nav_start_search(state);
}

void on_reading_goto_click(ui2_button_t *button, void *user_data) {
    (void)button;
    reader_state_t *state = (reader_state_t *)user_data;
    reader_nav_start_goto(state);
}

// List callbacks
void on_toc_list_selection_changed(int new_selection, void *user_data) {
    reader_state_t *state = (reader_state_t *)user_data;
    if (state) state->toc_selected = new_selection;
}

void on_toc_list_item_activated(int item_index, void *user_data) {
    (void)item_index;
    reader_state_t *state = (reader_state_t *)user_data;
    if (!state || state->toc_count <= 0 || !state->toc) return;
    if (state->toc_selected < 0 || state->toc_selected >= state->toc_count) return;
    const toc_entry_t *entry = &state->toc[state->toc_selected];
    state->mode = MODE_READING;
    fseek(state->file, entry->file_offset, SEEK_SET);
    page_cache_init(&state->page_cache);
    state->page_cache.entries[0].file_pos = entry->file_offset;
    state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
    state->page_cache.count = 1;
    state->page_cache.current = 0;
    state->page_number = entry->page_number;
}

void on_file_list_selection_changed(int new_selection, void *user_data) {
    reader_state_t *state = (reader_state_t *)user_data;
    if (state) state->file_selected = new_selection;
}

void on_file_list_item_activated(int item_index, void *user_data) {
    reader_state_t *state = (reader_state_t *)user_data;
    if (!state || item_index < 0 || item_index >= state->file_count) return;
    state->file_selected = item_index;
    int bp = 0, up = 0;
    if (reader_events_open_book(state, state->file_paths[state->file_selected], &bp, &up)) {
        reader_events_enter_reading_mode(state, &bp, &up);
    }
}

int reader_events_open_book(reader_state_t *state, const char *path, int *bold_pending, int *underline_pending) {
    *bold_pending = 0;
    *underline_pending = 0;
    return reader_open_file(state, path);
}

void reader_events_enter_reading_mode(reader_state_t *state, int *bold_pending, int *underline_pending) {
    state->mode = MODE_READING;
    state->screen_width = text_mode_get_cols() - MARGIN * 2;
    int rows = text_mode_get_rows();
    bool is_portrait = display_get_height() >= display_get_width();
    if (is_portrait) {
        state->content_rows = rows - 4;
    } else {
        state->content_rows = rows - 2;
    }

    // Init render pipeline with 3 dynamic buffers
    if (!state->pipeline.task) {
        int lb = state->screen_width + LINE_BUF_MARGIN;
        render_pipeline_init(&state->pipeline, state->current_file, state->file, state->screen_width, state->content_rows, lb);
    }
    if (state->pipeline.task) {
        state->lines = state->pipeline.buffers[state->pipeline.display_buffer].lines;
        state->line_buf_size = state->screen_width + LINE_BUF_MARGIN;
        render_pipeline_ensure_buffers(&state->pipeline, state->screen_width, state->content_rows);
    }

    if (!reader_alloc_lines(state, state->screen_width, state->content_rows)) {
        printf("EVENTS: Failed to allocate lines buffer!\n");
        return;
    }

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

    if (state->page_cache.entries[0].file_pos > 0 && state->page_number == 1) {
        state->page_number = reader_compute_page_number(state);
    }

    reader_load_current_page(state, bold_pending, underline_pending);
}

void reader_events_show_file_list(reader_state_t *state) {
    reader_scan_md_files(state);
    state->file_selected = 0;
    state->mode = MODE_FILE_LIST;
    reader_view_setup_file_list(state);
    ui2_screen_render(state->screen);
}

static void setup_and_render(reader_state_t *state, int *bold_pending, int *underline_pending) {
    switch (state->mode) {
        case MODE_FILE_LIST:
            reader_view_setup_file_list(state);
            ui2_screen_render(state->screen);
            break;
        case MODE_READING:
            reader_view_setup_reading(state);
            reader_view_render_reading(state, bold_pending, underline_pending);
            break;
        case MODE_TOC:
            reader_view_setup_toc(state);
            ui2_screen_render(state->screen);
            break;
        case MODE_GOTO:
            reader_view_setup_goto(state);
            ui2_screen_render(state->screen);
            break;
        case MODE_SEARCH:
            reader_view_setup_search(state);
            ui2_screen_render(state->screen);
            break;
        case MODE_RECEIVING:
            reader_view_setup_receiving(state);
            reader_view_render_receiving(state);
            break;
    }
}

void reader_events_handle_event(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void)) {
    state->launch_app_list = launch_app_list;

    if (event->type == EVENT_SERIAL) {
        if (state->mode == MODE_RECEIVING) {
            serial_rx_process_bytes(event->serial.data, event->serial.len);
        }
        return;
    }

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        if (ui2_osk_is_active()) {
            ui2_osk_handle_event(NULL, (event_t *)event);
            if (!ui2_osk_is_active()) {
                handle_osk_result(state, bold_pending, underline_pending);
            }
            return;
        }

        if (state->ignore_events > 0) {
            state->ignore_events--;
            return;
        }

        char key = event->keyboard.key;
        if ((event->keyboard.modifiers & MODIFIER_CTRL) && key >= 1 && key <= 26) {
            key = (char)('a' + key - 1);
        }

        reader_mode_t prev_mode = state->mode;

        if (state->mode == MODE_READING) {
            if (key == 'w' || key == 'W') {
                reader_nav_prev_page(state, bold_pending, underline_pending);
                reader_view_render_reading(state, bold_pending, underline_pending);
                return;
            }
            if (key == 's' || key == 'S') {
                reader_nav_next_page(state, bold_pending, underline_pending);
                reader_view_render_reading(state, bold_pending, underline_pending);
                return;
            }
            if (key == 'g' || key == 'G') {
                reader_nav_start_goto(state);
                setup_and_render(state, bold_pending, underline_pending);
                return;
            }
            if (key == '/') {
                reader_nav_start_search(state);
                setup_and_render(state, bold_pending, underline_pending);
                return;
            }
            if (key == 't' || key == 'T') {
                enter_toc_mode(state);
                setup_and_render(state, bold_pending, underline_pending);
                return;
            }
            if (key == 27) {
                exit_to_file_list(state);
                setup_and_render(state, bold_pending, underline_pending);
                return;
            }
            return;
        }

        ui2_screen_handle_event(state->screen, (event_t *)event);

        if (state->mode != prev_mode) {
            setup_and_render(state, bold_pending, underline_pending);
        } else if (key == 27) {
            if (state->mode == MODE_TOC) {
                state->mode = MODE_READING;
                setup_and_render(state, bold_pending, underline_pending);
            } else if (state->mode == MODE_FILE_LIST) {
                exit_to_file_list(state);
                setup_and_render(state, bold_pending, underline_pending);
            } else if (state->mode == MODE_RECEIVING) {
                cancel_receiving(state);
                setup_and_render(state, bold_pending, underline_pending);
            }
        } else if (state->mode == MODE_FILE_LIST || state->mode == MODE_TOC) {
            ui2_screen_render(state->screen);
        }
        return;
    }

    if (event->type == EVENT_TOUCH && event->touch.pressed) {
        if (ui2_osk_is_active()) {
            ui2_osk_handle_event(NULL, (event_t *)event);
            if (!ui2_osk_is_active()) {
                handle_osk_result(state, bold_pending, underline_pending);
            }
            return;
        }

        reader_mode_t prev_mode = state->mode;
        bool handled = ui2_screen_handle_event(state->screen, (event_t *)event);

        if (!handled && state->mode == MODE_READING && prev_mode == MODE_READING) {
            if (event->touch.x < TOUCH_PAGE_SPLIT_X) {
                reader_nav_prev_page(state, bold_pending, underline_pending);
            } else {
                reader_nav_next_page(state, bold_pending, underline_pending);
            }
            reader_view_render_reading(state, bold_pending, underline_pending);
            return;
        }

        if (state->mode != prev_mode) {
            setup_and_render(state, bold_pending, underline_pending);
        } else if (state->mode == MODE_FILE_LIST || state->mode == MODE_TOC) {
            ui2_screen_render(state->screen);
        }
        return;
    }
}
