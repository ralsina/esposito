#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "app_config.h"
#include "app_launcher.h"

#include <dirent.h>
#include <errno.h>
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

typedef struct {
    const char *app_name;
    const char *extensions[5];
} fm_app_assoc_t;

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
    char **display_list;  // Dynamic array of strings, exactly sized to entry_count
    int display_list_count;  // How many items are actually allocated in display_list
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
} file_manager_t;

static const char *TAG = "file_manager";
static file_manager_t state;

static const fm_app_assoc_t APP_ASSOCIATIONS[] = {
    {"reader", {".md", ".markdown", NULL}},
    {"kilo", {".txt", ".md", ".markdown", ".c", ".h"}},
    {"image_viewer", {".jpg", ".jpeg", NULL}},
};

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

static int path_has_extension(const char *path, const char *extension) {
    size_t path_len = strlen(path);
    size_t ext_len = strlen(extension);
    if (path_len < ext_len) return 0;
    return strcmp(path + path_len - ext_len, extension) == 0;
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

static void clear_pending_open(void) {
    state.pending_open_path[0] = '\0';
    state.pending_open_count = 0;
    for (int index = 0; index < FM_OPEN_WITH_MAX; index++) {
        state.pending_open_apps[index] = NULL;
    }
}

static void pane_clear_entries(fm_pane_t *pane);
static void pane_free_display_list(fm_pane_t *pane);

static void pane_clear_entries(fm_pane_t *pane) {
    pane->entry_count = 0;
    pane_free_display_list(pane);
}

static void pane_free_display_list(fm_pane_t *pane) {
    if (!pane || !pane->display_list) {
        pane->display_list_count = 0;
        return;
    }
    for (int i = 0; i < pane->display_list_count; i++) {
        if (pane->display_list[i]) {
            free(pane->display_list[i]);
        }
    }
    free(pane->display_list);
    pane->display_list = NULL;
    pane->display_list_count = 0;
}

static void pane_allocate_display_list(fm_pane_t *pane) {
    pane_free_display_list(pane);
    
    if (pane->entry_count <= 0) {
        pane->display_list = NULL;
        return;
    }
    
    size_t array_size = sizeof(char*) * (pane->entry_count + 1);
    
    pane->display_list = malloc(array_size);
    if (!pane->display_list) {
        os_log(TAG, "display list alloc failed: %zu bytes", array_size);
        set_status("Memory allocation failed");
        pane->display_list = NULL;
        return;
    }
    
    for (int i = 0; i < pane->entry_count; i++) {
        const fm_entry_t *ent = &pane->entries[i];
        size_t len = (ent->is_dir ? 4 : 4) + strlen(ent->name) + 1;
        pane->display_list[i] = malloc(len);
        if (!pane->display_list[i]) {
            os_log(TAG, "display item alloc failed at %d: %zu bytes", i, len);
            set_status("Memory allocation failed");
            pane_free_display_list(pane);
            return;
        }
        snprintf(pane->display_list[i], len, "%s%s", ent->is_dir ? "[D] " : "    ", ent->name);
    }
    pane->display_list[pane->entry_count] = NULL;
    pane->display_list_count = pane->entry_count;
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
    pane->selected = 0;
    pane->scroll = 0;

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
        // Saved pane path may no longer exist; retry from root once.
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

    pane_allocate_display_list(pane);
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
    int count = 0;
    for (size_t assoc_index = 0; assoc_index < sizeof(APP_ASSOCIATIONS) / sizeof(APP_ASSOCIATIONS[0]); assoc_index++) {
        for (int ext_index = 0; APP_ASSOCIATIONS[assoc_index].extensions[ext_index]; ext_index++) {
            if (path_has_extension(path, APP_ASSOCIATIONS[assoc_index].extensions[ext_index])) {
                if (count < max_apps) {
                    apps_out[count++] = APP_ASSOCIATIONS[assoc_index].app_name;
                }
                break;
            }
        }
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
    snprintf(message, sizeof(message), "Open with: 1:%s 2:%s", apps[0], apps[1]);
    set_status(message);
}

static int handle_pending_open_choice(char key) {
    if (state.pending_open_count <= 0) return 0;

    if (key == 27) {
        clear_pending_open();
        set_status("Open-with canceled");
        return 1;
    }

    if (key < '1' || key > '9') return 1;

    int choice = key - '1';
    if (choice < 0 || choice >= state.pending_open_count) return 1;

    const char *app_name = state.pending_open_apps[choice];
    char file_path[FM_MAX_PATH];
    snprintf(file_path, sizeof(file_path), "%s", state.pending_open_path);
    clear_pending_open();
    open_selected_with_app(app_name, file_path);
    return 1;
}


static void draw_pane(int pane_index, int x, int width, int height) {
    fm_pane_t *pane = &state.panes[pane_index];
    int active = pane_index == state.active_pane;

    char title[FM_MAX_PATH + 16];
    char path_display[FM_MAX_PATH + 1];
    int path_max = width - 12;
    if (path_max < 3) path_max = 3;

    snprintf(path_display, sizeof(path_display), "%s", pane->cwd);
    if ((int)strlen(path_display) > path_max) {
        int start = (int)strlen(path_display) - path_max + 1;
        if (start < 0) start = 0;
        snprintf(path_display, sizeof(path_display), "~%s", pane->cwd + start);
    }

    snprintf(title, sizeof(title), "%c %s", active ? '*' : ' ', path_display);

    ui_column_draw(x, 0, width, height, title, active,
                   (const char **)pane->display_list, pane->entry_count, pane->selected, pane->scroll);
}

static void render(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int pane_height = rows - 2;
    int left_width = cols / 2;
    int right_width = cols - left_width;

    draw_pane(0, 0, left_width, pane_height);
    draw_pane(1, left_width, right_width, pane_height);

    fm_pane_t *active = &state.panes[state.active_pane];
    int selected_index = pane_selected_index(active);
    char right[32];
    snprintf(right, sizeof(right), "P%d %d/%d", state.active_pane + 1,
             selected_index < 0 ? 0 : selected_index + 1,
             active->entry_count);

    ui_status_bar(rows - 2, state.status, right);
    ui_label(1, rows - 1, "W/S move A/D switch Enter open K mkdir C copy O open-with", TEXT_COLOR_BRIGHT_BLACK);

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
        return;
    }

    char message[FM_STATUS_MAX];
    snprintf(message, sizeof(message), "File: %s (%u bytes)", entry->name, entry->size);
    set_status(message);
}

static void active_up_or_exit(void) {
    fm_pane_t *pane = &state.panes[state.active_pane];
    if (!path_is_root(pane->cwd)) {
        path_parent(pane->cwd, pane->cwd, sizeof(pane->cwd));
        pane_scan_directory(pane);
        set_status("Parent directory");
        return;
    }

    app_launcher_start();
}

static void save_state(void) {
    if (!config_bind_app("file_manager")) return;

    config_set_string("left_dir", state.panes[0].cwd);
    config_set_string("right_dir", state.panes[1].cwd);
    config_set_int("active_pane", state.active_pane);
    config_unbind_app();
}

void app_init(app_context_t *ctx) {
    if (!text_mode_init()) {
        os_log(TAG, "text_mode_init failed");
        return;
    }

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

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
        render();
        return;
    }
    state.panes[0].display_list = NULL;
    state.panes[0].display_list_count = 0;
    state.panes[1].display_list = NULL;
    state.panes[1].display_list_count = 0;
    state.active_pane = 0;
    clear_pending_open();

    int config_ok = config_bind_app("file_manager");
    
    if (config_ok) {
        config_get_string("left_dir", FM_ROOT_PATH, state.panes[0].cwd, sizeof(state.panes[0].cwd));
        config_get_string("right_dir", FM_ROOT_PATH, state.panes[1].cwd, sizeof(state.panes[1].cwd));
        state.active_pane = config_get_int("active_pane", 0);
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
    render();
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) {
        return;
    }

    char key = event->keyboard.key;
    fm_pane_t *active = &state.panes[state.active_pane];

    if (handle_pending_open_choice(key)) {
        render();
        return;
    }

    if (key == 'w' || key == 'W') {
        if (active->entry_count > 0) {
            if (active->selected > 0) {
                active->selected--;
            } else {
                active->selected = active->entry_count - 1;
            }
        }
    } else if (key == 's' || key == 'S') {
        if (active->entry_count > 0) {
            if (active->selected < active->entry_count - 1) {
                active->selected++;
            } else {
                active->selected = 0;
            }
        }
    } else if (key == 'a' || key == 'A') {
        state.active_pane = 0;
        set_status("Active pane: left");
    } else if (key == 'd' || key == 'D' || key == '\t') {
        state.active_pane = 1;
        set_status("Active pane: right");
    } else if (key == '\n' || key == '\r') {
        active_open_selected();
    } else if (key == 27) {
        active_up_or_exit();
    } else if (key == 'r' || key == 'R') {
        pane_scan_directory(active);
        set_status("Reloaded");
    } else if (key == 'k' || key == 'K') {
        active_mkdir();
    } else if (key == 'c' || key == 'C') {
        active_copy_to_other_pane();
    } else if (key == 'o' || key == 'O') {
        active_open_with();
    }

    render();
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
    save_state();
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    save_state();
    for (int pane_index = 0; pane_index < FM_PANES; pane_index++) {
        pane_free_display_list(&state.panes[pane_index]);
        if (state.panes[pane_index].entries) {
            free(state.panes[pane_index].entries);
            state.panes[pane_index].entries = NULL;
        }
        state.panes[pane_index].entries_capacity = 0;
    }
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "File Manager close");
}
