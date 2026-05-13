#include "os_core.h"
#include "checkpoint.h"
#include "text_mode.h"
#include "reader_md.h"
#include "reader_state.h"
#include "reader_core.h"
#include "ui.h"
extern void app_launcher_start(void);
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#define TOUCH_PAGE_SPLIT_X 160

#define FILE_LIST_BTN_WIDTH 13
#define FILE_LIST_BTN_GAP 2
#define FILE_LIST_BTN_UP_LABEL "  UP  "
#define FILE_LIST_BTN_OPEN_LABEL " OPEN "
#define FILE_LIST_BTN_DOWN_LABEL " DOWN "
#define FILE_LIST_BTN_EXIT_LABEL " EXIT "

#define READING_CLOSE_BUTTON_WIDTH 4

static reader_state_t state;
static int bold_pending = 0;
static int underline_pending = 0;

static int load_current_page(void);
static void draw_reading_page(void);
static void draw_file_list(void);
static int open_book(const char *path);

static void enter_reading_mode(void) {
    state.mode = MODE_READING;
    state.screen_width = text_mode_get_cols() - MARGIN * 2;
    state.content_rows = text_mode_get_rows() - 2;
    load_current_page();
    draw_reading_page();
}

static void change_file_selection(int delta) {
    int next = state.file_selected + delta;
    if (next < 0 || next >= state.file_count) {
        return;
    }
    state.file_selected = next;
    draw_file_list();
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

static void draw_rich_line(int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t base_attr) {
    int cur_x = x;
    uint8_t attr = base_attr;
    int bold_active = 0;
    int underline_active = 0;
    if (bold_pending) {
        attr = base_attr | TEXT_ATTR_BOLD;
        bold_active = 1;
        bold_pending = 0;
    }
    if (underline_pending) {
        attr |= TEXT_ATTR_UNDERLINE;
        underline_active = 1;
        underline_pending = 0;
    }
    while (*text) {
        if (text[0] == '*' && text[1] == '*') {
            attr = (attr & TEXT_ATTR_BOLD) ? base_attr : (base_attr | TEXT_ATTR_BOLD);
            text += 2;
            bold_active = (attr & TEXT_ATTR_BOLD) ? 1 : 0;
            continue;
        }
        if (*text == MD_LINK_TOGGLE) {
            if (attr & TEXT_ATTR_UNDERLINE) {
                attr &= ~TEXT_ATTR_UNDERLINE;
                underline_active = 0;
            } else {
                attr |= TEXT_ATTR_UNDERLINE;
                underline_active = 1;
            }
            text++;
            continue;
        }
        char buf[2] = {*text, '\0'};
        text_mode_print_at_attr_bg(cur_x, y, buf, fg, bg, attr);
        cur_x++;
        text++;
    }
    if (bold_active) bold_pending = 1;
    if (underline_active) underline_pending = 1;
}

static void draw_reading_page(void) {
    ui_clear();

    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();

    // Top separator bar with underline attribute
    const char *fname = state.current_file;
    const char *slash = strrchr(fname, '/');
    if (slash) fname = slash + 1;

    char page_info[48];
    snprintf(page_info, sizeof(page_info), "Page %d", state.page_number);

    for (int x = 0; x < cols; x++) {
        text_mode_print_at_attr(x, 0, "-", TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    }
    text_mode_print_at_attr(1, 0, fname, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);
    int info_x = cols - 3 - (int)strlen(page_info);
    if (info_x > 0) {
        text_mode_print_at_attr(info_x, 0, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    }
    text_mode_print_at_attr_bg(cols - 1, 0, "X", TEXT_COLOR_GREEN, TEXT_COLOR_BLACK, TEXT_ATTR_INVERSE);

    // Render text (offset by 2 for separator + blank line)
    for (int i = 0; i < state.line_count && i < state.content_rows; i++) {
        rendered_line_t *rl = &state.lines[i];
        if (rl->text[0] == '\0') {
            bold_pending = 0;
            underline_pending = 0;
        }
        draw_rich_line(MARGIN, 2 + i, rl->text, rl->color, TEXT_COLOR_BLACK, rl->attr);
    }


}

static void next_page(void) {
    if (page_cache_can_next(&state.page_cache)) {
        page_cache_next(&state.page_cache);
    } else {
        uint32_t next_offset = ftell(state.file);
        page_cache_add_next(&state.page_cache, next_offset);
        page_cache_next(&state.page_cache);
    }
    state.page_number++;
    load_current_page();
    draw_reading_page();
}

static void goto_page(int target);

static void prev_page(void) {
    if (page_cache_can_prev(&state.page_cache)) {
        page_cache_prev(&state.page_cache);
        state.page_number--;
        load_current_page();
        draw_reading_page();
    } else if (state.page_number > 1) {
        int cols = text_mode_get_cols();
        int rows = text_mode_get_rows();
        for (int y = 2; y < rows; y++)
            for (int x = 0; x < cols; x++)
                text_mode_print_at_attr_bg(x, y, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
        text_mode_print_at_attr((cols - 10) / 2, rows / 2, "Loading...", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        goto_page(state.page_number - 1);
    }
}

static void goto_page(int target) {
    if (target < 1) return;
    md_clear_remainder();
    fseek(state.file, 0, SEEK_SET);

    uint32_t page_starts[PAGE_CACHE_ENTRIES];
    int store_count = 0;
    int store_pos = 0;

    int last_page = 0;
    int page;
    for (page = 1; page < target; page++) {
        uint32_t start = ftell(state.file);
        int n = md_scan_page(state.file, state.lines, state.content_rows, state.screen_width);
        if (n == 0) break;
        last_page = page;
        page_starts[store_pos] = start;
        store_pos = (store_pos + 1) % PAGE_CACHE_ENTRIES;
        if (store_count < PAGE_CACHE_ENTRIES) store_count++;
    }

    uint32_t offset;
    int actual_page;
    if (last_page == 0) {
        offset = 0;
        actual_page = 1;
        fseek(state.file, 0, SEEK_SET);
    } else if (page < target) {
        // Compute offset from ring buffer
        int idx = (store_pos - 1 + PAGE_CACHE_ENTRIES) % PAGE_CACHE_ENTRIES;
        offset = page_starts[idx];
        actual_page = last_page;
        fseek(state.file, offset, SEEK_SET);
    } else {
        long probe_pos = ftell(state.file);
        md_clear_remainder();
        int probe_n = md_scan_page(state.file, state.lines, state.content_rows, state.screen_width);
        if (probe_n == 0) {
            int idx = (store_pos - 1 + PAGE_CACHE_ENTRIES) % PAGE_CACHE_ENTRIES;
            offset = page_starts[idx];
            actual_page = last_page;
            fseek(state.file, offset, SEEK_SET);
        } else {
            offset = (uint32_t)probe_pos;
            actual_page = target;
            fseek(state.file, probe_pos, SEEK_SET);
            // Store target page start in ring buffer
            page_starts[store_pos] = offset;
            store_pos = (store_pos + 1) % PAGE_CACHE_ENTRIES;
            if (store_count < PAGE_CACHE_ENTRIES) store_count++;
        }
    }

    // Populate page cache from ring buffer: oldest first, current last
    page_cache_init(&state.page_cache);
    int copy_count = store_count;
    if (copy_count > 0) {
        state.page_cache.count = copy_count;
        state.page_cache.current = copy_count - 1;
        for (int i = 0; i < copy_count; i++) {
            int src = (store_pos - copy_count + i + PAGE_CACHE_ENTRIES) % PAGE_CACHE_ENTRIES;
            state.page_cache.offsets[i] = page_starts[src];
        }
    } else {
        page_cache_set_start(&state.page_cache, offset);
    }

    md_clear_remainder();
    state.page_number = actual_page;
    load_current_page();
    draw_reading_page();
}

static void draw_goto_prompt(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    char prompt[24];
    int n = snprintf(prompt, sizeof(prompt), "Go to page: %s", state.goto_buf);
    if (n < (int)sizeof(prompt) - 1) {
        prompt[n] = '_';
        prompt[n + 1] = '\0';
    }
    for (int x = 0; x < cols; x++) {
        text_mode_print_at_attr_bg(x, rows - 1, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);
    }
    text_mode_print_at_attr_bg(0, rows - 1, prompt, TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);
}

static void draw_file_list(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_rows = rows - 5;

    ui_clear();
    ui_window(0, 0, cols, rows, "Select a Book");
    ui_menu_draw(1, 1, list_rows, state.file_ptrs, state.file_count, state.file_selected);

    int btn_row = rows - 3;
    int btn_w = FILE_LIST_BTN_WIDTH;
    int gap = FILE_LIST_BTN_GAP;

    char spacer[64];
    memset(spacer, ' ', btn_w);
    spacer[btn_w] = '\0';

    int up_x = 2;
    int open_x = up_x + btn_w + gap;
    int down_x = open_x + btn_w + gap;
    int exit_x = down_x + btn_w + gap;

    text_mode_print_at_attr_bg(up_x, btn_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(up_x + (btn_w - 6) / 2, btn_row, FILE_LIST_BTN_UP_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(open_x, btn_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(open_x + (btn_w - 6) / 2, btn_row, FILE_LIST_BTN_OPEN_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(down_x, btn_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(down_x + (btn_w - 6) / 2, btn_row, FILE_LIST_BTN_DOWN_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(exit_x, btn_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_RED, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(exit_x + (btn_w - 6) / 2, btn_row, FILE_LIST_BTN_EXIT_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_RED, TEXT_ATTR_NORMAL);

    state.btn_up_x = up_x;
    state.btn_open_x = open_x;
    state.btn_down_x = down_x;
    state.btn_exit_x = exit_x;
    state.btn_row = btn_row;
    state.btn_w = btn_w;
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
    draw_file_list();
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
        prev_page();
    } else if (key == 's' || key == 'S') {
        next_page();
    } else if (key == 'g' || key == 'G') {
        state.mode = MODE_GOTO;
        state.goto_pos = 0;
        state.goto_buf[0] = '\0';
        draw_goto_prompt();
    } else if (key == 27) {
        exit_to_file_list();
    }
}

static void handle_goto_event(char key) {
    if (key >= '0' && key <= '9') {
        if (state.goto_pos < (int)sizeof(state.goto_buf) - 1) {
            state.goto_buf[state.goto_pos++] = key;
            state.goto_buf[state.goto_pos] = '\0';
            draw_goto_prompt();
        }
    } else if (key == '\n' || key == '\r') {
        state.mode = MODE_READING;
        int page = atoi(state.goto_buf);
        if (page > 1) goto_page(page);
        else goto_page(1);
    } else if (key == 27) {
        state.mode = MODE_READING;
        draw_reading_page();
    }
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
    draw_file_list();
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
        prev_page();
    } else {
        next_page();
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
