#include "os_core.h"
#include "app_config.h"
#include "app_launcher.h"
#include "text_mode.h"
#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define FP_MAX_ENTRIES 128
#define FP_MAX_NAME 96
#define FP_MAX_PATH 192
#define FP_STATUS_MAX 128
#define FP_GLOB_MAX 96
#define FP_TITLE_MAX 64
#define FP_APP_MAX 32
#define FP_KEY_MAX 64

typedef struct {
    char name[FP_MAX_NAME];
    char path[FP_MAX_PATH];
    unsigned int is_dir;
} fp_entry_t;

typedef struct {
    char root_path[FP_MAX_PATH];
    char cwd[FP_MAX_PATH];
    char glob[FP_GLOB_MAX];
    char title[FP_TITLE_MAX];
    char return_app[FP_APP_MAX];
    char target_app[FP_APP_MAX];
    char result_key[FP_KEY_MAX];
    int cancel_to_launcher;

    fp_entry_t entries[FP_MAX_ENTRIES];
    int entry_count;
    int selected;
    int scroll;
    char status[FP_STATUS_MAX];
} file_picker_t;

static const char *TAG = "file_picker";
static file_picker_t picker;

static int ascii_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static void set_status(const char *message) {
    if (!message) message = "";
    snprintf(picker.status, sizeof(picker.status), "%s", message);
}

static int is_under_root(const char *path) {
    size_t root_len = strlen(picker.root_path);
    if (strncmp(path, picker.root_path, root_len) != 0) {
        return 0;
    }
    if (path[root_len] == '\0' || path[root_len] == '/') {
        return 1;
    }
    return 0;
}

static int is_root(const char *path) {
    return strcmp(path, picker.root_path) == 0;
}

static void path_parent(const char *path, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", path);

    size_t len = strlen(out);
    while (len > strlen(picker.root_path) && out[len - 1] == '/') {
        out[len - 1] = '\0';
        len--;
    }

    char *slash = strrchr(out, '/');
    if (!slash || (size_t)(slash - out) < strlen(picker.root_path)) {
        snprintf(out, out_size, "%s", picker.root_path);
        return;
    }

    *slash = '\0';
    if (!is_under_root(out)) {
        snprintf(out, out_size, "%s", picker.root_path);
    }
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void path_dirname(const char *path, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", path ? path : "");

    char *slash = strrchr(out, '/');
    if (!slash) {
        out[0] = '\0';
        return;
    }

    if (slash == out) {
        out[1] = '\0';
        return;
    }

    *slash = '\0';
}

static int entry_compare(const fp_entry_t *left, const fp_entry_t *right) {
    if (strcmp(left->name, "..") == 0) return -1;
    if (strcmp(right->name, "..") == 0) return 1;

    if (left->is_dir != right->is_dir) {
        return left->is_dir ? -1 : 1;
    }

    const char *left_name = left->name;
    const char *right_name = right->name;
    while (*left_name && *right_name) {
        int left_lower = ascii_tolower((unsigned char)*left_name);
        int right_lower = ascii_tolower((unsigned char)*right_name);
        if (left_lower != right_lower) {
            return left_lower - right_lower;
        }
        left_name++;
        right_name++;
    }
    return ascii_tolower((unsigned char)*left_name) - ascii_tolower((unsigned char)*right_name);
}

static int glob_match_one(const char *pattern, const char *text) {
    const char *pattern_ptr = pattern;
    const char *text_ptr = text;
    const char *star_pattern = NULL;
    const char *star_text = NULL;

    while (*text_ptr) {
        if (*pattern_ptr == '*') {
            star_pattern = ++pattern_ptr;
            star_text = text_ptr;
        } else if (*pattern_ptr == '?' || ascii_tolower((unsigned char)*pattern_ptr) == ascii_tolower((unsigned char)*text_ptr)) {
            pattern_ptr++;
            text_ptr++;
        } else if (star_pattern) {
            pattern_ptr = star_pattern;
            text_ptr = ++star_text;
        } else {
            return 0;
        }
    }

    while (*pattern_ptr == '*') {
        pattern_ptr++;
    }

    return *pattern_ptr == '\0';
}

static int matches_glob(const char *filename) {
    if (!picker.glob[0] || strcmp(picker.glob, "*") == 0) {
        return 1;
    }

    char patterns[FP_GLOB_MAX];
    snprintf(patterns, sizeof(patterns), "%s", picker.glob);

    char token[FP_GLOB_MAX];
    int token_len = 0;
    for (int index = 0;; index++) {
        char ch = patterns[index];
        int at_end = ch == '\0';
        int is_separator = (ch == '|' || ch == ',' || ch == ';');

        if (!at_end && !is_separator) {
            if (token_len < (int)sizeof(token) - 1) {
                token[token_len++] = ch;
            }
            continue;
        }

        while (token_len > 0 && token[token_len - 1] == ' ') {
            token_len--;
        }
        token[token_len] = '\0';

        char *start = token;
        while (*start == ' ') {
            start++;
        }

        if (*start && glob_match_one(start, filename)) {
            return 1;
        }

        token_len = 0;

        if (at_end) {
            break;
        }
    }

    return 0;
}

static int add_entry(const char *name, const char *path, int is_dir) {
    if (picker.entry_count >= FP_MAX_ENTRIES) {
        return 0;
    }

    fp_entry_t *entry = &picker.entries[picker.entry_count++];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->is_dir = is_dir ? 1U : 0U;
    return 1;
}

static void sort_entries(void) {
    int sort_start = 0;
    if (picker.entry_count > 0 && strcmp(picker.entries[0].name, "..") == 0) {
        sort_start = 1;
    }

    for (int index = sort_start + 1; index < picker.entry_count; index++) {
        fp_entry_t value = picker.entries[index];
        int position = index - 1;
        while (position >= sort_start && entry_compare(&picker.entries[position], &value) > 0) {
            picker.entries[position + 1] = picker.entries[position];
            position--;
        }
        picker.entries[position + 1] = value;
    }
}

static void scan_directory(void) {
    picker.entry_count = 0;
    picker.selected = 0;
    picker.scroll = 0;

    if (!is_under_root(picker.cwd) || !path_exists(picker.cwd)) {
        snprintf(picker.cwd, sizeof(picker.cwd), "%s", picker.root_path);
    }

    if (!is_root(picker.cwd)) {
        char parent[FP_MAX_PATH];
        path_parent(picker.cwd, parent, sizeof(parent));
        add_entry("..", parent, 1);
    }

    DIR *directory = opendir(picker.cwd);
    if (!directory) {
        set_status("Cannot open directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[FP_MAX_PATH];
        int written = snprintf(full_path, sizeof(full_path), "%s/%s", picker.cwd, entry->d_name);
        if (written <= 0 || written >= (int)sizeof(full_path)) {
            continue;
        }

        struct stat path_stat;
        if (stat(full_path, &path_stat) != 0) {
            continue;
        }

        int is_dir = S_ISDIR(path_stat.st_mode) ? 1 : 0;
        if (!is_dir && !matches_glob(entry->d_name)) {
            continue;
        }

        if (!add_entry(entry->d_name, full_path, is_dir)) {
            set_status("Directory too large");
            break;
        }
    }

    closedir(directory);

    if (picker.entry_count > 1) {
        sort_entries();
    }
}

static void ensure_selection_visible(int list_rows) {
    if (picker.selected < 0) picker.selected = 0;
    if (picker.selected >= picker.entry_count && picker.entry_count > 0) {
        picker.selected = picker.entry_count - 1;
    }

    if (picker.selected < picker.scroll) {
        picker.scroll = picker.selected;
    }
    if (picker.selected >= picker.scroll + list_rows) {
        picker.scroll = picker.selected - list_rows + 1;
    }
    if (picker.scroll < 0) picker.scroll = 0;
}

static void picker_return(int canceled) {
    if (canceled && picker.cancel_to_launcher) {
        app_launcher_start();
        return;
    }

    if (!picker.return_app[0] || !os_load_app(picker.return_app)) {
        app_launcher_start();
    }
}

static void select_current_file(void) {
    if (picker.entry_count <= 0) {
        set_status("No selection");
        return;
    }

    fp_entry_t *entry = &picker.entries[picker.selected];
    if (entry->is_dir) {
        snprintf(picker.cwd, sizeof(picker.cwd), "%s", entry->path);
        scan_directory();
        return;
    }

    if (!picker.target_app[0] || !picker.result_key[0]) {
        set_status("Picker not configured");
        return;
    }

    if (!config_bind_app(picker.target_app)) {
        set_status("Cannot bind target app");
        return;
    }

    config_set_string(picker.result_key, entry->path);
    config_unbind_app();

    picker_return(0);
}

static void draw_row(int x, int y, int width, const fp_entry_t *entry, int selected) {
    char line[160];
    if (entry->is_dir) {
        snprintf(line, sizeof(line), "%c [D] %s", selected ? '>' : ' ', entry->name);
    } else {
        snprintf(line, sizeof(line), "%c     %s", selected ? '>' : ' ', entry->name);
    }

    int max_text = width - 1;
    if (max_text < 0) max_text = 0;
    if ((int)strlen(line) > max_text) {
        line[max_text] = '\0';
    }

    uint8_t fg = selected ? TEXT_COLOR_BLACK : TEXT_COLOR_WHITE;
    uint8_t bg = selected ? TEXT_COLOR_BRIGHT_GREEN : TEXT_COLOR_BLACK;
    uint8_t attr = selected ? TEXT_ATTR_BOLD : TEXT_ATTR_NORMAL;

    text_mode_print_at_attr_bg(x, y, line, fg, bg, attr);

    int used = (int)strlen(line);
    for (int col = used; col < width; col++) {
        text_mode_print_at_attr_bg(x + col, y, " ", fg, bg, attr);
    }
}

static void render(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int body_height = rows - 2;
    int list_rows = body_height - 2;
    if (list_rows < 1) list_rows = 1;

    char title_line[FP_TITLE_MAX + FP_GLOB_MAX + 8];
    snprintf(title_line, sizeof(title_line), "%s", picker.title[0] ? picker.title : "File Picker");

    ui_window(0, 0, cols, body_height, title_line);

    ensure_selection_visible(list_rows);

    for (int row = 0; row < list_rows; row++) {
        int index = picker.scroll + row;
        if (index < picker.entry_count) {
            draw_row(1, 1 + row, cols - 2, &picker.entries[index], index == picker.selected);
        } else {
            for (int col = 0; col < cols - 2; col++) {
                text_mode_print_at_attr_bg(1 + col, 1 + row, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
            }
        }
    }

    char right[64];
    snprintf(right, sizeof(right), "%s", picker.glob[0] ? picker.glob : "*");
    ui_status_bar(rows - 2, picker.status, right);
    ui_label(1, rows - 1, "W/S move  Enter select  ESC up/cancel  R reload", TEXT_COLOR_BRIGHT_BLACK);
    text_mode_flush();
}

static void load_config(void) {
    snprintf(picker.root_path, sizeof(picker.root_path), "/sdcard");
    snprintf(picker.cwd, sizeof(picker.cwd), "%s", picker.root_path);
    snprintf(picker.glob, sizeof(picker.glob), "*");
    snprintf(picker.title, sizeof(picker.title), "File Picker");
    picker.return_app[0] = '\0';
    picker.target_app[0] = '\0';
    picker.result_key[0] = '\0';
    picker.cancel_to_launcher = 0;

    if (!config_bind_app("file_picker")) {
        return;
    }

    config_get_string("root_path", picker.root_path, picker.root_path, sizeof(picker.root_path));
    config_get_string("start_path", picker.root_path, picker.cwd, sizeof(picker.cwd));
    config_get_string("glob", "*", picker.glob, sizeof(picker.glob));
    config_get_string("title", "File Picker", picker.title, sizeof(picker.title));
    config_get_string("return_app", "", picker.return_app, sizeof(picker.return_app));
    config_get_string("target_app", "", picker.target_app, sizeof(picker.target_app));
    config_get_string("result_key", "", picker.result_key, sizeof(picker.result_key));
    picker.cancel_to_launcher = config_get_int("cancel_to_launcher", 0);

    config_unbind_app();

    if (!picker.root_path[0]) {
        snprintf(picker.root_path, sizeof(picker.root_path), "/sdcard");
    }

    if (!is_under_root(picker.root_path) || !path_exists(picker.root_path)) {
        snprintf(picker.root_path, sizeof(picker.root_path), "/sdcard");
    }

    if (!is_under_root(picker.cwd) || !path_exists(picker.cwd)) {
        snprintf(picker.cwd, sizeof(picker.cwd), "%s", picker.root_path);
    } else {
        struct stat st;
        if (stat(picker.cwd, &st) == 0 && S_ISREG(st.st_mode)) {
            path_dirname(picker.cwd, picker.cwd, sizeof(picker.cwd));
            if (!picker.cwd[0] || !is_under_root(picker.cwd)) {
                snprintf(picker.cwd, sizeof(picker.cwd), "%s", picker.root_path);
            }
        }
    }
}

void app_init(app_context_t *ctx) {
    (void)ctx;

    os_log(TAG, "File Picker init");

    if (!text_mode_init()) {
        os_log(TAG, "text_mode_init failed");
        return;
    }

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    memset(&picker, 0, sizeof(picker));
    load_config();
    set_status("Select a file");
    scan_directory();
    render();
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) {
        return;
    }

    char key = event->keyboard.key;

    if (key == 'w' || key == 'W') {
        if (picker.selected > 0) picker.selected--;
    } else if (key == 's' || key == 'S') {
        if (picker.selected + 1 < picker.entry_count) picker.selected++;
    } else if (key == '\n' || key == '\r') {
        select_current_file();
    } else if (key == 'r' || key == 'R') {
        scan_directory();
        set_status("Reloaded");
    } else if (key == 27) {
        if (!is_root(picker.cwd)) {
            path_parent(picker.cwd, picker.cwd, sizeof(picker.cwd));
            scan_directory();
        } else {
            picker_return(1);
            return;
        }
    }

    render();
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "File Picker close");
}
