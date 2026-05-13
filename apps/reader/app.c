#include "os_core.h"
#include "checkpoint.h"
#include "text_mode.h"
#include "reader_page.h"
#include "reader_md.h"
#include "ui.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#define MARGIN 2
#define MAX_FILES 64
#define MAX_PATH 256

typedef enum {
    MODE_FILE_LIST,
    MODE_READING,
} reader_mode_t;

typedef struct {
    reader_mode_t mode;

    // File list state
    int file_count;
    char file_names[MAX_FILES][64];
    char file_paths[MAX_FILES][MAX_PATH];
    const char *file_ptrs[MAX_FILES];
    int file_selected;

    // Reading state
    char current_file[MAX_PATH];
    FILE *file;
    page_cache_t page_cache;
    rendered_line_t lines[MAX_RENDERED_LINES];
    int line_count;
    int screen_width;
    int content_rows;
} reader_state_t;

static reader_state_t state;

static void scan_md_files(void) {
    state.file_count = 0;
    DIR *dir = opendir("/sdcard/books");
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && state.file_count < MAX_FILES) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        size_t len = strlen(entry->d_name);
        if (len < 3 || strcmp(entry->d_name + len - 3, ".md") != 0) continue;

        strncpy(state.file_names[state.file_count], entry->d_name, 63);
        state.file_names[state.file_count][63] = '\0';
        snprintf(state.file_paths[state.file_count], MAX_PATH, "/sdcard/books/%s", entry->d_name);
        state.file_ptrs[state.file_count] = state.file_names[state.file_count];
        state.file_count++;
    }
    closedir(dir);
}

static void close_current_file(void) {
    if (state.file) {
        fclose(state.file);
        state.file = NULL;
    }
    state.current_file[0] = '\0';
}

static int open_file(const char *path) {
    close_current_file();
    md_clear_remainder();
    state.file = fopen(path, "r");
    if (!state.file) return 0;
    strncpy(state.current_file, path, MAX_PATH - 1);
    state.current_file[MAX_PATH - 1] = '\0';
    page_cache_init(&state.page_cache);
    page_cache_set_start(&state.page_cache, 0);
    return 1;
}

static int load_current_page(void) {
    if (!state.file) return 0;
    uint32_t offset = page_cache_current_offset(&state.page_cache);
    fseek(state.file, offset, SEEK_SET);
    state.line_count = md_scan_page(state.file, state.lines, state.content_rows, state.screen_width);
    return state.line_count;
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
    snprintf(page_info, sizeof(page_info), "Page %d  W:prev S:next", page_cache_page_number(&state.page_cache));

    for (int x = 0; x < cols; x++) {
        text_mode_print_at_attr(x, 0, "-", TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    }
    text_mode_print_at_attr(1, 0, fname, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);
    int info_x = cols - 2 - (int)strlen(page_info);
    if (info_x > 0) {
        text_mode_print_at_attr(info_x, 0, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    }

    // Render text (offset by 2 for separator + blank line)
    for (int i = 0; i < state.line_count && i < state.content_rows; i++) {
        rendered_line_t *rl = &state.lines[i];
        text_mode_print_at_attr_bg(MARGIN, 2 + i, rl->text, rl->color, TEXT_COLOR_BLACK, rl->attr);
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
    load_current_page();
    draw_reading_page();
}

static void prev_page(void) {
    if (!page_cache_can_prev(&state.page_cache)) return;
    page_cache_prev(&state.page_cache);
    load_current_page();
    draw_reading_page();
}

static void draw_file_list(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_rows = rows - 3;

    ui_clear();
    ui_window(0, 0, cols, rows, "Select a Book");
    ui_menu_draw(1, 1, list_rows, state.file_ptrs, state.file_count, state.file_selected);
    ui_status_bar(rows - 1, "W/S: nav  ENTER: open", "ESC: exit");
}

static void handle_file_list_event(char key) {
    if (key == 'w' || key == 'W') {
        if (state.file_selected > 0) {
            state.file_selected--;
            draw_file_list();
        }
    } else if (key == 's' || key == 'S') {
        if (state.file_selected < state.file_count - 1) {
            state.file_selected++;
            draw_file_list();
        }
    } else if (key == '\n' || key == '\r') {
        if (state.file_count > 0 && open_file(state.file_paths[state.file_selected])) {
            state.mode = MODE_READING;
            state.screen_width = text_mode_get_cols() - MARGIN * 2;
            state.content_rows = text_mode_get_rows() - 2;
            load_current_page();
            draw_reading_page();
        }
    }
}

static void handle_reading_event(char key) {
    if (key == 'w' || key == 'W') {
        prev_page();
    } else if (key == 's' || key == 'S') {
        next_page();
    } else if (key == 27) {
        state.mode = MODE_FILE_LIST;
        close_current_file();
        draw_file_list();
    }
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    memset(&state, 0, sizeof(state));
    text_mode_init();
    checkpoint_open("reader");

    // Try checkpoint restore
    const char *saved_file = checkpoint_load_string("reader_file");
    int saved_offset = checkpoint_load_int("reader_offset");

    if (saved_file && saved_file[0] && saved_offset >= 0) {
        struct stat st;
        if (stat(saved_file, &st) == 0 && S_ISREG(st.st_mode)) {
            if (open_file(saved_file)) {
                state.mode = MODE_READING;
                state.screen_width = text_mode_get_cols() - MARGIN * 2;
                state.content_rows = text_mode_get_rows() - 2;
                page_cache_set_start(&state.page_cache, (uint32_t)saved_offset);
                load_current_page();
                draw_reading_page();
                return;
            }
        }
    }

    // Show file list
    scan_md_files();
    state.file_selected = 0;
    state.mode = MODE_FILE_LIST;
    draw_file_list();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (!(event->type == EVENT_KEYBOARD && event->keyboard.pressed)) return;
    char key = event->keyboard.key;

    if (state.mode == MODE_FILE_LIST) {
        handle_file_list_event(key);
    } else {
        handle_reading_event(key);
    }
}

void app_checkpoint(app_context_t *ctx) {
    if (state.mode == MODE_READING && state.current_file[0]) {
        checkpoint_save_string("reader_file", state.current_file);
        checkpoint_save_int("reader_offset", (int)page_cache_current_offset(&state.page_cache));
    }
}

void app_close(app_context_t *ctx) {
    checkpoint_close();
    close_current_file();
    text_mode_clear(TEXT_COLOR_BLACK);
}
