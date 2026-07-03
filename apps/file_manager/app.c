#include "os_core.h"
#include "text_mode.h"
#include "ui2.h"
#include "ui2_toolbar.h"
#include "ui2_osk.h"
#include "app_config.h"
#include "app_manifest.h"
#include "hardware.h"
#include "lucide_icons.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FM_ROOT_PATH "/sdcard"
#define FM_MAX_ENTRIES 96
#define FM_MAX_NAME 96
#define FM_MAX_PATH 192
#define FM_STATUS_MAX 128
#define FM_PANES 2
#define FM_OPEN_WITH_MAX 4
static const char FM_OPEN_WITH_KEYS[] = "qwertyuiop";

typedef struct {
    char name[FM_MAX_NAME];
    char path[FM_MAX_PATH];
    unsigned int is_dir;
    unsigned int size;
} fm_entry_t;

typedef struct {
    char cwd[FM_MAX_PATH];
    fm_entry_t *entries;
    int entries_capacity;
    int entry_count;
    int selected;
    int scroll;
} fm_pane_t;

typedef struct {
    fm_pane_t panes[FM_PANES];
    int active_pane;
    char status[FM_STATUS_MAX];
    char pending_open_path[FM_MAX_PATH];
    const char *pending_open_apps[FM_OPEN_WITH_MAX];
    int pending_open_count;
    int input_mode;
    int osk_mode;
    int pending_edit_pane;
    int pending_edit_is_dir;
    char pending_edit_path[FM_MAX_PATH];
    char pending_name[FM_MAX_NAME];
    ui2_text_input_t *name_input;
    ui2_screen_t *screen;
    ui2_list_t *lists[FM_PANES];
} file_manager_t;

static const char *TAG = "file_manager";
static file_manager_t state;

static char pane_display[FM_PANES][FM_MAX_ENTRIES][FM_MAX_NAME + 2];
static const char *pane_ptrs[FM_PANES][FM_MAX_ENTRIES];
static uint8_t pane_row_attrs[FM_PANES][FM_MAX_ENTRIES];

static void render(void);
static void apply_name_input(void);
static void on_name_confirm(void *user_data);
static void on_name_cancel(void *user_data);
static void on_new_file_click(ui2_button_t *button, void *user_data);
static void on_mkdir_click(ui2_button_t *button, void *user_data);
static void on_rename_click(ui2_button_t *button, void *user_data);
static void on_copy_click(ui2_button_t *button, void *user_data);
static void on_delete_click(ui2_button_t *button, void *user_data);
static void on_exit_click(ui2_button_t *button, void *user_data);
static void on_open_click(ui2_button_t *button, void *user_data);

static void start_new_file(void);
static void active_mkdir(void);
static void start_rename_selected(void);
static void active_copy_to_other_pane(void);
static void active_delete_selected(void);
static void active_up_or_exit(void);
static void active_open_selected(void);

static void trim_spaces(char *text) {
    if (!text || !text[0]) {
        return;
    }

    size_t start = 0;
    while (text[start] == ' ' || text[start] == '\t') {
        start++;
    }

    if (start > 0) {
        memmove(text, text + start, strlen(text + start) + 1);
    }

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len--;
    }
}

enum {
    INPUT_MODE_NONE = 0,
    INPUT_MODE_NEW_FILE,
    INPUT_MODE_RENAME,
};

static char manifest_app_bufs[FM_OPEN_WITH_MAX][256];

static int ascii_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static int path_is_under_root(const char *path) {
    if (!path || !path[0]) return 0;
    return strncmp(path, FM_ROOT_PATH, strlen(FM_ROOT_PATH)) == 0;
}

static int path_is_root(const char *path) {
    return strcmp(path, FM_ROOT_PATH) == 0;
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void path_parent(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return;

    if (!path_is_under_root(path) || path_is_root(path)) {
        snprintf(out, out_size, "%s", FM_ROOT_PATH);
        return;
    }

    snprintf(out, out_size, "%s", path);
    size_t len = strlen(out);

    while (len > strlen(FM_ROOT_PATH) && out[len - 1] == '/') {
        out[len - 1] = '\0';
        len--;
    }

    char *slash = strrchr(out, '/');
    if (!slash || slash == out || (size_t)(slash - out) < strlen(FM_ROOT_PATH)) {
        snprintf(out, out_size, "%s", FM_ROOT_PATH);
        return;
    }

    *slash = '\0';
    if (!path_is_under_root(out)) {
        snprintf(out, out_size, "%s", FM_ROOT_PATH);
    }
}

static int entry_compare(const fm_entry_t *left, const fm_entry_t *right) {
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

static void pane_sort_entries(fm_pane_t *pane) {
    int sort_start = 0;
    if (pane->entry_count > 0 && strcmp(pane->entries[0].name, "..") == 0) {
        sort_start = 1;
    }

    for (int index = sort_start + 1; index < pane->entry_count; index++) {
        fm_entry_t value = pane->entries[index];
        int position = index - 1;
        while (position >= sort_start && entry_compare(&pane->entries[position], &value) > 0) {
            pane->entries[position + 1] = pane->entries[position];
            position--;
        }
        pane->entries[position + 1] = value;
    }
}

static void set_status(const char *message) {
    if (!message) message = "";
    snprintf(state.status, sizeof(state.status), "%s", message);
}

static void on_new_file_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    start_new_file();
}

static void on_mkdir_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    active_mkdir();
}

static void on_rename_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    start_rename_selected();
}

static void on_copy_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    active_copy_to_other_pane();
}

static void on_delete_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    active_delete_selected();
}

static void on_exit_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    active_up_or_exit();
}

static void on_open_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    active_open_selected();
}

static void clear_pending_open(void) {
    state.pending_open_path[0] = '\0';
    state.pending_open_count = 0;
    for (int index = 0; index < FM_OPEN_WITH_MAX; index++) {
        state.pending_open_apps[index] = NULL;
    }
}

static void clear_pending_edit(void) {
    state.input_mode = INPUT_MODE_NONE;
    state.osk_mode = INPUT_MODE_NONE;
    state.pending_edit_pane = 0;
    state.pending_edit_is_dir = 0;
    state.pending_edit_path[0] = '\0';
    state.pending_name[0] = '\0';
}

static void pane_clear_entries(fm_pane_t *pane) {
    pane->entry_count = 0;
    pane->selected = 0;
    pane->scroll = 0;
}

static int pane_add_entry(fm_pane_t *pane, const char *name, const char *path, int is_dir, unsigned int size) {
    if (!pane->entries || pane->entry_count >= pane->entries_capacity) {
        return 0;
    }

    fm_entry_t *entry = &pane->entries[pane->entry_count++];
    snprintf(entry->name, sizeof(entry->name), "%s", name ? name : "");
    snprintf(entry->path, sizeof(entry->path), "%s", path ? path : "");
    entry->is_dir = is_dir ? 1U : 0U;
    entry->size = size;
    return 1;
}

static void pane_scan_directory(fm_pane_t *pane) {
    pane_clear_entries(pane);

    if (!path_is_under_root(pane->cwd)) {
        snprintf(pane->cwd, sizeof(pane->cwd), "%s", FM_ROOT_PATH);
    }

    if (!path_is_root(pane->cwd)) {
        char parent[FM_MAX_PATH];
        path_parent(pane->cwd, parent, sizeof(parent));
        pane_add_entry(pane, "..", parent, 1, 0);
    }

    DIR *directory = opendir(pane->cwd);
    if (!directory) {
        if (!path_is_root(pane->cwd)) {
            snprintf(pane->cwd, sizeof(pane->cwd), "%s", FM_ROOT_PATH);
            directory = opendir(pane->cwd);
        }
        if (!directory) {
            set_status("Cannot open directory");
            return;
        }
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[FM_MAX_PATH];
        int written = snprintf(full_path, sizeof(full_path), "%s/%s", pane->cwd, entry->d_name);
        if (written <= 0 || written >= (int)sizeof(full_path)) {
            continue;
        }

        struct stat path_stat;
        if (stat(full_path, &path_stat) != 0) {
            continue;
        }

        unsigned int is_dir = S_ISDIR(path_stat.st_mode) ? 1U : 0U;
        unsigned int size = is_dir ? 0U : (unsigned int)path_stat.st_size;
        if (!pane_add_entry(pane, entry->d_name, full_path, is_dir, size)) {
            set_status("Directory too large, partial list");
            break;
        }
    }

    closedir(directory);

    if (pane->entry_count > 1) {
        pane_sort_entries(pane);
    }
}

static int pane_selected_index(fm_pane_t *pane) {
    if (!pane || pane->entry_count <= 0) {
        return -1;
    }
    if (pane->selected < 0 || pane->selected >= pane->entry_count) {
        return -1;
    }
    return pane->selected;
}

static void pane_select_path(fm_pane_t *pane, const char *path) {
    if (!pane || !path || path[0] == '\0') {
        return;
    }

    for (int index = 0; index < pane->entry_count; index++) {
        if (strcmp(pane->entries[index].path, path) == 0) {
            pane->selected = index;
            return;
        }
    }
}

static int make_unique_path(const char *directory, const char *base_name, char *out, size_t out_size) {
    char candidate[FM_MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%s/%s", directory, base_name);
    if (!path_exists(candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return 1;
    }

    for (int suffix = 2; suffix < 1000; suffix++) {
        snprintf(candidate, sizeof(candidate), "%s/%s_%d", directory, base_name, suffix);
        if (!path_exists(candidate)) {
            snprintf(out, out_size, "%s", candidate);
            return 1;
        }
    }
    return 0;
}

static int copy_file(const char *source_path, const char *dest_path) {
    FILE *source = fopen(source_path, "rb");
    if (!source) return 0;

    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        fclose(source);
        return 0;
    }

    char buffer[512];
    size_t read_len;
    while ((read_len = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, read_len, dest) != read_len) {
            fclose(source);
            fclose(dest);
            return 0;
        }
    }

    fclose(source);
    fclose(dest);
    return 1;
}

static void open_selected_with_app(const char *app_name, const char *file_path) {
    if (!os_open_app_with_file(app_name, file_path)) {
        char message[FM_STATUS_MAX];
        snprintf(message, sizeof(message), "Failed to launch %s", app_name);
        set_status(message);
    }
}

static int collect_apps_for_path(const char *path, const char **apps_out, int max_apps) {
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) return 0;
    const char *ext = dot + 1;

    int n = max_apps < FM_OPEN_WITH_MAX ? max_apps : FM_OPEN_WITH_MAX;
    int count = app_manifest_find_apps_for_ext(ext, manifest_app_bufs, n);
    for (int i = 0; i < count; i++) {
        apps_out[i] = manifest_app_bufs[i];
    }
    return count;
}

static void active_mkdir(void) {
    fm_pane_t *pane = &state.panes[state.active_pane];
    char new_path[FM_MAX_PATH];

    if (!make_unique_path(pane->cwd, "newdir", new_path, sizeof(new_path))) {
        set_status("mkdir: no free name");
        return;
    }

    if (mkdir(new_path, 0777) != 0) {
        set_status("mkdir failed");
        return;
    }

    pane_scan_directory(pane);
    set_status("Directory created");
    render();
}

static void active_copy_to_other_pane(void) {
    fm_pane_t *source_pane = &state.panes[state.active_pane];
    fm_pane_t *target_pane = &state.panes[1 - state.active_pane];

    int selected_index = pane_selected_index(source_pane);
    if (selected_index < 0) {
        set_status("Nothing selected");
        return;
    }

    fm_entry_t *entry = &source_pane->entries[selected_index];
    if (entry->is_dir) {
        set_status("Copy dir not supported yet");
        return;
    }

    char destination[FM_MAX_PATH];
    if (!make_unique_path(target_pane->cwd, path_basename(entry->path), destination, sizeof(destination))) {
        set_status("copy: no free destination");
        return;
    }

    if (!copy_file(entry->path, destination)) {
        set_status("Copy failed");
        return;
    }

    pane_scan_directory(target_pane);
    set_status("Copied to other pane");
    render();
}

static void active_open_with(void) {
    fm_pane_t *pane = &state.panes[state.active_pane];
    int selected_index = pane_selected_index(pane);
    if (selected_index < 0) {
        set_status("Nothing selected");
        return;
    }

    fm_entry_t *entry = &pane->entries[selected_index];
    if (entry->is_dir) {
        set_status("Open-with files only");
        return;
    }

    const char *apps[FM_OPEN_WITH_MAX];
    int app_count = collect_apps_for_path(entry->path, apps, FM_OPEN_WITH_MAX);
    if (app_count <= 0) {
        set_status("No app for extension");
        return;
    }

    if (app_count == 1) {
        open_selected_with_app(apps[0], entry->path);
        return;
    }

    clear_pending_open();
    snprintf(state.pending_open_path, sizeof(state.pending_open_path), "%s", entry->path);
    state.pending_open_count = app_count;
    for (int index = 0; index < app_count; index++) {
        state.pending_open_apps[index] = apps[index];
    }

    char message[FM_STATUS_MAX];
    snprintf(message, sizeof(message), "Open with: %c:%s %c:%s",
             FM_OPEN_WITH_KEYS[0], apps[0], FM_OPEN_WITH_KEYS[1], apps[1]);
    set_status(message);
}

static int build_child_path(const char *directory, const char *name, char *out, size_t out_size) {
    if (!directory || !name || !out || out_size == 0 || name[0] == '\0') {
        return 0;
    }

    if (strchr(name, '/')) {
        return 0;
    }

    int written = snprintf(out, out_size, "%s/%s", directory, name);
    if (written <= 0 || written >= (int)out_size) {
        return 0;
    }

    return path_is_under_root(out);
}

static void start_new_file(void) {
    clear_pending_edit();
    state.input_mode = INPUT_MODE_NEW_FILE;
    snprintf(state.pending_name, sizeof(state.pending_name), "%s", "newfile.txt");

    if (!keyboard_is_available()) {
        state.osk_mode = state.input_mode;
        state.input_mode = INPUT_MODE_NONE;
        ui2_osk_input_text("New File:", state.pending_name, sizeof(state.pending_name), "newfile.txt", false);
        return;
    }

    render();
}

static void start_rename_selected(void) {
    fm_pane_t *pane = &state.panes[state.active_pane];
    int selected_index = pane_selected_index(pane);
    if (selected_index < 0) {
        set_status("Nothing selected");
        return;
    }

    fm_entry_t *entry = &pane->entries[selected_index];
    if (strcmp(entry->name, "..") == 0) {
        set_status("Cannot rename parent entry");
        return;
    }

    clear_pending_edit();
    state.input_mode = INPUT_MODE_RENAME;
    state.pending_edit_pane = state.active_pane;
    state.pending_edit_is_dir = entry->is_dir;
    snprintf(state.pending_edit_path, sizeof(state.pending_edit_path), "%s", entry->path);
    snprintf(state.pending_name, sizeof(state.pending_name), "%s", entry->name);

    if (!keyboard_is_available()) {
        ui2_osk_input_text("Rename:", state.pending_name, sizeof(state.pending_name), entry->name, false);
        return;
    }

    render();
}

static void active_delete_selected(void) {
    fm_pane_t *pane = &state.panes[state.active_pane];
    int selected_index = pane_selected_index(pane);
    if (selected_index < 0) {
        set_status("Nothing selected");
        return;
    }

    fm_entry_t *entry = &pane->entries[selected_index];
    if (strcmp(entry->name, "..") == 0) {
        set_status("Cannot delete parent entry");
        return;
    }

    if (remove(entry->path) != 0) {
        set_status(entry->is_dir ? "Delete failed (dir not empty?)" : "Delete failed");
        return;
    }

    pane_scan_directory(pane);
    set_status(entry->is_dir ? "Directory deleted" : "File deleted");
    render();
}

static void on_name_confirm(void *user_data) {
    (void)user_data;
    apply_name_input();
}

static void on_name_cancel(void *user_data) {
    (void)user_data;
    clear_pending_edit();
    set_status("Canceled");
    render();
}

static void apply_name_input(void) {
    trim_spaces(state.pending_name);
    if (state.pending_name[0] == '\0') {
        set_status("Name cannot be empty");
        clear_pending_edit();
        render();
        return;
    }

    if (state.input_mode == INPUT_MODE_NEW_FILE) {
        fm_pane_t *pane = &state.panes[state.active_pane];
        char path[FM_MAX_PATH];
        if (!build_child_path(pane->cwd, state.pending_name, path, sizeof(path))) {
            set_status("Invalid file name");
        } else if (path_exists(path)) {
            set_status("File already exists");
        } else {
            FILE *file = fopen(path, "wb");
            if (!file) {
                set_status("Create file failed");
            } else {
                fclose(file);
                pane_scan_directory(pane);
                set_status("File created");
            }
        }
    } else if (state.input_mode == INPUT_MODE_RENAME) {
        fm_pane_t *pane = &state.panes[state.pending_edit_pane];
        char new_path[FM_MAX_PATH];
        if (!build_child_path(pane->cwd, state.pending_name, new_path, sizeof(new_path))) {
            set_status("Invalid target name");
        } else if (strcmp(new_path, state.pending_edit_path) == 0) {
            set_status("Name unchanged");
        } else if (path_exists(new_path)) {
            set_status("Target already exists");
        } else if (rename(state.pending_edit_path, new_path) != 0) {
            set_status("Rename failed");
        } else {
            pane_scan_directory(&state.panes[0]);
            pane_scan_directory(&state.panes[1]);
            set_status(state.pending_edit_is_dir ? "Directory renamed" : "File renamed");
        }
    }

    clear_pending_edit();
    render();
}

static int handle_pending_open_choice(char key) {
    if (state.pending_open_count <= 0) return 0;

    if (key == 27) {
        clear_pending_open();
        set_status("Open-with canceled");
        return 1;
    }

    int choice = -1;
    for (int index = 0; FM_OPEN_WITH_KEYS[index] != '\0'; index++) {
        if (key == FM_OPEN_WITH_KEYS[index] || key == (char)(FM_OPEN_WITH_KEYS[index] - 'a' + 'A')) {
            choice = index;
            break;
        }
    }

    if (choice < 0 || choice >= state.pending_open_count) return 1;

    const char *app_name = state.pending_open_apps[choice];
    char file_path[FM_MAX_PATH];
    snprintf(file_path, sizeof(file_path), "%s", state.pending_open_path);
    clear_pending_open();
    open_selected_with_app(app_name, file_path);
    return 1;
}

static void build_pane_display(int pane_index) {
    fm_pane_t *pane = &state.panes[pane_index];
    for (int i = 0; i < pane->entry_count && i < FM_MAX_ENTRIES; i++) {
        const fm_entry_t *ent = &pane->entries[i];
        snprintf(pane_display[pane_index][i], sizeof(pane_display[0][0]), "%s%s",
                 ent->name, ent->is_dir ? "/" : "");
        pane_ptrs[pane_index][i] = pane_display[pane_index][i];
        pane_row_attrs[pane_index][i] = ent->is_dir ? TEXT_ATTR_BOLD : TEXT_ATTR_NORMAL;
    }
}

static void on_pane_selection_changed(int new_selection, void *user_data) {
    int pane_idx = (int)(intptr_t)user_data;
    state.panes[pane_idx].selected = new_selection;
}

static void on_pane_item_activated(int item_index, void *user_data) {
    int pane_idx = (int)(intptr_t)user_data;
    state.active_pane = pane_idx;
    state.panes[pane_idx].selected = item_index;
    active_open_selected();
}

static void draw_status_overlay(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int status_row = rows - 1;
    char line[FM_STATUS_MAX + 4];
    int len = strlen(state.status);
    if (len > cols) len = cols;
    int pos = 0;
    for (int i = 0; i < len; i++) line[pos++] = state.status[i];
    for (int i = pos; i < cols; i++) line[pos++] = ' ';
    line[pos] = '\0';
    text_mode_print_at_attr_bg(0, status_row, line,
                               TEXT_COLOR_BRIGHT_BLACK, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
}

static void render(void) {
    if (ui2_osk_is_active()) return;
    if (state.input_mode != INPUT_MODE_NONE) {
        text_mode_clear(TEXT_COLOR_BLACK);
        ui2_text_input_set_buffer(state.name_input, state.pending_name, sizeof(state.pending_name));
        UI2_WIDGET(state.name_input)->vtable->draw(UI2_WIDGET(state.name_input));
        text_mode_flush();
        return;
    }

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int pane_height = rows - 5;
    int left_width = cols / 2;
    int right_width = cols - left_width - 1;

    for (int p = 0; p < 2; p++) {
        fm_pane_t *pane = &state.panes[p];
        build_pane_display(p);

        ui2_widget_t *lw = UI2_WIDGET(state.lists[p]);
        lw->x = p == 0 ? 0 : left_width + 1;
        lw->width = p == 0 ? left_width : right_width;
        lw->height = pane_height;

        ui2_list_set_title(state.lists[p], pane->cwd);
        ui2_list_set_items(state.lists[p], pane_ptrs[p], pane->entry_count);
        ui2_list_set_row_attrs(state.lists[p], pane_row_attrs[p], pane->entry_count);
        ui2_list_set_selection(state.lists[p], pane->selected);
    }

    ui2_screen_render(state.screen);
    draw_status_overlay();
    text_mode_flush();
}

static void active_open_selected(void) {
    fm_pane_t *pane = &state.panes[state.active_pane];
    int selected_index = pane_selected_index(pane);
    if (selected_index < 0) {
        set_status("No entries");
        return;
    }

    fm_entry_t *entry = &pane->entries[selected_index];
    if (entry->is_dir) {
        snprintf(pane->cwd, sizeof(pane->cwd), "%s", entry->path);
        pane_scan_directory(pane);
        set_status("Entered directory");
        render();
        return;
    }

    active_open_with();
}

static void active_up_or_exit(void) {
    fm_pane_t *pane = &state.panes[state.active_pane];
    if (!path_is_root(pane->cwd)) {
        path_parent(pane->cwd, pane->cwd, sizeof(pane->cwd));
        pane_scan_directory(pane);
        set_status("Parent directory");
        render();
        return;
    }
    os_exit();
}


static void save_state(void) {
    if (!config_bind_app("file_manager")) return;

    config_set_string("left_dir", state.panes[0].cwd);
    config_set_string("right_dir", state.panes[1].cwd);
    config_set_int("active_pane", state.active_pane);

    int left_selected_index = pane_selected_index(&state.panes[0]);
    int right_selected_index = pane_selected_index(&state.panes[1]);
    const char *left_selected_path = "";
    const char *right_selected_path = "";

    if (left_selected_index >= 0) {
        left_selected_path = state.panes[0].entries[left_selected_index].path;
    }
    if (right_selected_index >= 0) {
        right_selected_path = state.panes[1].entries[right_selected_index].path;
    }

    config_set_string("left_selected", left_selected_path);
    config_set_string("right_selected", right_selected_path);
    config_unbind_app();
}

void app_init(app_context_t *ctx) {
    if (!text_mode_init()) {
        os_log(TAG, "text_mode_init failed");
        return;
    }

    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;

    snprintf(state.panes[0].cwd, sizeof(state.panes[0].cwd), "%s", FM_ROOT_PATH);
    snprintf(state.panes[1].cwd, sizeof(state.panes[1].cwd), "%s", FM_ROOT_PATH);
    state.panes[0].entries = calloc(FM_MAX_ENTRIES, sizeof(fm_entry_t));
    state.panes[1].entries = calloc(FM_MAX_ENTRIES, sizeof(fm_entry_t));
    state.panes[0].entries_capacity = FM_MAX_ENTRIES;
    state.panes[1].entries_capacity = FM_MAX_ENTRIES;
    if (!state.panes[0].entries || !state.panes[1].entries) {
        os_log(TAG, "Failed to allocate pane entries");
        if (state.panes[0].entries) {
            free(state.panes[0].entries);
            state.panes[0].entries = NULL;
        }
        if (state.panes[1].entries) {
            free(state.panes[1].entries);
            state.panes[1].entries = NULL;
        }
        set_status("Out of memory allocating pane entries");
        return;
    }
    state.active_pane = 0;
    clear_pending_open();
    clear_pending_edit();

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    state.screen = ui2_screen_create();
    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_screen_set_root(state.screen, root);

    int left_width = cols / 2;
    int right_width = cols - left_width - 1;
    int pane_height = rows - 6;

    for (int p = 0; p < 2; p++) {
        int x = p == 0 ? 0 : left_width + 1;
        int w = p == 0 ? left_width : right_width;
        state.lists[p] = ui2_list_create(x, 0, w, pane_height);
        ui2_list_set_colors(state.lists[p], TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                            TEXT_COLOR_BLACK, TEXT_COLOR_BRIGHT_GREEN, TEXT_COLOR_CYAN);
        ui2_list_set_border(state.lists[p], true);
        ui2_list_set_callbacks(state.lists[p], on_pane_selection_changed,
                               on_pane_item_activated, (void*)(intptr_t)p);
        ui2_layout_add(root, UI2_WIDGET(state.lists[p]));
    }
    ui2_screen_focus_set(state.screen, UI2_WIDGET(state.lists[0]));

    int btn_row = rows - 4;
    ui2_toolbar_item_t btn_defs[] = {
        {ICON_FILE_PLUS,  on_new_file_click, NULL},
        {ICON_FOLDER_PLUS, on_mkdir_click,   NULL},
        {ICON_EDIT_2,     on_rename_click,   NULL},
        {ICON_COPY,       on_copy_click,     NULL},
        {ICON_TRASH_2,    on_delete_click,   NULL},
        {ICON_CHECK,      on_open_click,     NULL},
        {ICON_X,          on_exit_click,     NULL},
    };
    ui2_layout_t *bar = ui2_toolbar_create(0, btn_row, cols, 3, btn_defs, 7);
    ui2_layout_add(root, UI2_WIDGET(bar));

    state.name_input = ui2_text_input_create(0, rows - 5, cols, 4);
    ui2_text_input_set_title(state.name_input, "File Manager");
    ui2_text_input_set_label(state.name_input, "Name:");
    ui2_text_input_set_hints(state.name_input, "Enter Confirm", "ESC Cancel");
    ui2_text_input_set_callbacks(state.name_input, on_name_confirm, on_name_cancel, NULL);

    int config_ok = config_bind_app("file_manager");
    char left_selected[FM_MAX_PATH];
    char right_selected[FM_MAX_PATH];
    left_selected[0] = '\0';
    right_selected[0] = '\0';

    if (config_ok) {
        config_get_string("left_dir", FM_ROOT_PATH, state.panes[0].cwd, sizeof(state.panes[0].cwd));
        config_get_string("right_dir", FM_ROOT_PATH, state.panes[1].cwd, sizeof(state.panes[1].cwd));
        state.active_pane = config_get_int("active_pane", 0);
        config_get_string("left_selected", "", left_selected, sizeof(left_selected));
        config_get_string("right_selected", "", right_selected, sizeof(right_selected));

        config_unbind_app();
    }

    if (state.active_pane < 0 || state.active_pane >= FM_PANES) {
        state.active_pane = 0;
    }

    for (int pane_index = 0; pane_index < FM_PANES; pane_index++) {
        if (!path_is_under_root(state.panes[pane_index].cwd)) {
            snprintf(state.panes[pane_index].cwd, sizeof(state.panes[pane_index].cwd), "%s", FM_ROOT_PATH);
        }
        pane_scan_directory(&state.panes[pane_index]);
    }

    pane_select_path(&state.panes[0], left_selected);
    pane_select_path(&state.panes[1], right_selected);

    ui2_screen_focus_set(state.screen, UI2_WIDGET(state.lists[state.active_pane]));
    render();
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;

    if (ui2_osk_is_active()) {
        ui2_osk_handle_event(ctx, event);
        if (!ui2_osk_is_active()) {
            if (ui2_osk_get_result() == UI2_OSK_RESULT_CONFIRMED) {
                state.input_mode = state.osk_mode;
                state.osk_mode = INPUT_MODE_NONE;
                apply_name_input();
            } else {
                clear_pending_edit();
                set_status("Canceled");
                render();
            }
        }
        return;
    }

    if (event->type == EVENT_TOUCH) {
        if (ui2_screen_handle_event(state.screen, event))
            render();
        return;
    }

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) {
        return;
    }

    char key = event->keyboard.key;

    if (state.input_mode != INPUT_MODE_NONE) {
        bool handled = UI2_WIDGET(state.name_input)->vtable->handle_key(UI2_WIDGET(state.name_input), key);
        if (handled) {
            text_mode_clear(TEXT_COLOR_BLACK);
            UI2_WIDGET(state.name_input)->vtable->draw(UI2_WIDGET(state.name_input));
            text_mode_flush();
        }
        return;
    }

    if (handle_pending_open_choice(key)) {
        render();
        return;
    }

    bool handled = ui2_screen_handle_event(state.screen, event);

    if (!handled) {
        fm_pane_t *active = &state.panes[state.active_pane];

        if (key == 'a' || key == 'A') {
            state.active_pane = 0;
            ui2_screen_focus_set(state.screen, UI2_WIDGET(state.lists[0]));
            set_status("Active pane: left");
            render();
        } else if (key == 'd' || key == 'D') {
            state.active_pane = 1;
            ui2_screen_focus_set(state.screen, UI2_WIDGET(state.lists[1]));
            set_status("Active pane: right");
            render();
        } else if (key == 'r' || key == 'R') {
            pane_scan_directory(active);
            set_status("Reloaded");
            render();
        } else if (key == 'k' || key == 'K') {
            active_mkdir();
        } else if (key == 'n' || key == 'N') {
            start_new_file();
        } else if (key == 'm' || key == 'M') {
            start_rename_selected();
        } else if (key == 'c' || key == 'C') {
            active_copy_to_other_pane();
        } else if (key == 'x' || key == 'X') {
            active_delete_selected();
        } else if (key == 27) {
            active_up_or_exit();
        }
    } else {
        render();
    }
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
    save_state();
}

void app_close(app_context_t *ctx) {
    (void)ctx;

    if (state.screen) {
        ui2_screen_destroy(state.screen);
        state.screen = NULL;
    }

    for (int pane_index = 0; pane_index < FM_PANES; pane_index++) {
        if (state.panes[pane_index].entries) {
            free(state.panes[pane_index].entries);
            state.panes[pane_index].entries = NULL;
        }
        state.panes[pane_index].entries_capacity = 0;
    }
    os_log(TAG, "File Manager close");
}
