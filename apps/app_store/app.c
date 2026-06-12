#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"
#include "core_json.h"
#include "ui2.h"
#include "lucide_icons.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

#define CATALOG_URL  "https://esposito.ralsina.me/apps/catalog.json"
#define CATALOG_PATH "/sdcard/.appstore_cat.json"
#define ELF_BASE     "https://esposito.ralsina.me/apps/"
#define SD_APPS_DIR  "/sdcard/apps"

#define MAX_APPS      64
#define DISPLAY_LEN   64
#define CATALOG_SIZE  32768
#define CATALOG_CACHE_HOURS 24

#define INSTALLING_KEY "os/installing_id"

typedef struct {
    char id[48];
    char name[64];
    char short_description[128];
    char long_description[512];
    char homepage[256];
    char version[16];
    char requires[128];
    char extensions[128];
    int size;
    char elf_url[256];
} catalog_app_t;

typedef enum {
    STATUS_NEW,
    STATUS_INSTALLED,
    STATUS_UPDATE,
} app_status_t;

typedef enum {
    MODE_CATALOG,
    MODE_DOWNLOADING,
    MODE_ERROR,
    MODE_DETAIL,
    MODE_CONFIRM_UNINSTALL,
} app_mode_t;

static ui2_screen_t *screen;
static ui2_list_t *app_list;
static int app_count;

static char *list_items[MAX_APPS];
static char (*list_texts)[DISPLAY_LEN];
static uint8_t row_attrs[MAX_APPS];
static app_status_t statuses[MAX_APPS];
static char app_ids[MAX_APPS][48];
static char app_names[MAX_APPS][64];
static char app_requires[MAX_APPS][128];
static char app_version[MAX_APPS][16];
static int app_sizes[MAX_APPS];
static app_mode_t mode;
static char error_msg[64];
static int ignore_events;
static char download_sd_path[320];
static char download_url[768];
static int uninstall_index;
static int detail_index;

static void on_app_activated(int index, void *data);
static void on_up_click(ui2_button_t *btn, void *data);
static void on_down_click(ui2_button_t *btn, void *data);
static void on_action_click(ui2_button_t *btn, void *data);
static void on_exit_click(ui2_button_t *btn, void *data);
static void do_install(int index);
static void do_uninstall(int index);
static void go_back_to_catalog(void);
static void show_confirm_uninstall(int index);
static void on_confirm_yes_click(ui2_button_t *btn, void *data);
static void on_confirm_no_click(ui2_button_t *btn, void *data);
static void on_install_click(ui2_button_t *btn, void *data);
static void on_uninstall_click(ui2_button_t *btn, void *data);
static void on_back_click(ui2_button_t *btn, void *data);
static void rebuild_catalog_ui(void);
static void show_detail(int index);
static int catalog_read_one(int index, catalog_app_t *app);
static app_status_t check_app_status_at(int index);

static void delete_dir_contents(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    char full_path[256];
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        remove(full_path);
    }
    closedir(dir);
}

static int extract_string(const char *val, size_t vallen, char *out, size_t out_size) {
    if (!val || !out || out_size == 0) return 0;
    int start = 0;
    int end = (int)vallen;
    if (val[0] == '"') start = 1;
    if (end > 0 && val[end - 1] == '"') end--;
    int len = end - start;
    if (len < 0) len = 0;
    if ((size_t)len >= out_size) len = (int)out_size - 1;
    memcpy(out, val + start, len);
    out[len] = '\0';
    return len;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_catalog_fresh(void) {
    struct stat st;
    if (stat(CATALOG_PATH, &st) != 0) return false;
    time_t now = time(NULL);
    time_t cache_age_sec = now - st.st_mtime;
    time_t cache_limit_sec = CATALOG_CACHE_HOURS * 3600;
    return cache_age_sec < cache_limit_sec;
}

static int file_get_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int)st.st_size;
}

static bool is_app_installed(const char *app_id) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/program.elf", SD_APPS_DIR, app_id);
    return file_exists(path);
}

static void get_installed_version(const char *app_id, char *version_out, size_t out_size) {
    version_out[0] = '\0';
    char path[256];
    snprintf(path, sizeof(path), "/sdcard/apps/%s/manifest.cfg", app_id);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, "version") == 0) {
            strncpy(version_out, eq + 1, out_size - 1);
            version_out[out_size - 1] = '\0';
            break;
        }
    }
    fclose(f);
}

static bool has_missing_caps(int index) {
    if (!app_requires[index][0]) return false;
    char caps[128];
    strncpy(caps, app_requires[index], sizeof(caps) - 1);
    caps[sizeof(caps) - 1] = '\0';
    char *p = caps;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        char *start = p;
        while (*p && *p != ',') p++;
        char saved = *p;
        *p = '\0';
        char *end = p - 1;
        while (end > start && *end == ' ') end--;
        end[1] = '\0';
        if (start[0] && !os_has_capability(start)) {
            return true;
        }
        if (saved == '\0') break;
        p++;
    }
    return false;
}

static app_status_t check_app_status_at(int index) {
    if (!is_app_installed(app_ids[index])) return STATUS_NEW;

    if (app_version[index][0]) {
        char installed_version[16];
        get_installed_version(app_ids[index], installed_version, sizeof(installed_version));
        if (installed_version[0] && strcmp(installed_version, app_version[index]) != 0) {
            return STATUS_UPDATE;
        }
    }

    return STATUS_INSTALLED;
}

static int catalog_read_one(int index, catalog_app_t *app) {
    FILE *fp = fopen(CATALOG_PATH, "r");
    if (!fp) return -1;
    char *buf = malloc(CATALOG_SIZE);
    if (!buf) { fclose(fp); return -1; }
    size_t len = fread(buf, 1, CATALOG_SIZE - 1, fp);
    fclose(fp);
    if (len == 0) { free(buf); return -1; }
    buf[len] = '\0';

    char query[32]; char *val; size_t vallen; JSONTypes_t type;
    snprintf(query, sizeof(query), "[%d].id", index);
    JSONStatus_t s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    if (s != JSONSuccess) { free(buf); return -1; }
    extract_string(val, vallen, app->id, sizeof(app->id));

    snprintf(query, sizeof(query), "[%d].name", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    if (s == JSONSuccess) extract_string(val, vallen, app->name, sizeof(app->name));
    else strncpy(app->name, app->id, sizeof(app->name) - 1);

    snprintf(query, sizeof(query), "[%d].short_description", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    extract_string(val, vallen, app->short_description, sizeof(app->short_description));

    snprintf(query, sizeof(query), "[%d].long_description", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    extract_string(val, vallen, app->long_description, sizeof(app->long_description));

    snprintf(query, sizeof(query), "[%d].homepage", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    extract_string(val, vallen, app->homepage, sizeof(app->homepage));

    snprintf(query, sizeof(query), "[%d].version", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    if (s == JSONSuccess) extract_string(val, vallen, app->version, sizeof(app->version));
    else app->version[0] = '\0';

    snprintf(query, sizeof(query), "[%d].requires", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    if (s == JSONSuccess) extract_string(val, vallen, app->requires, sizeof(app->requires));
    else app->requires[0] = '\0';

    snprintf(query, sizeof(query), "[%d].extensions", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    if (s == JSONSuccess) extract_string(val, vallen, app->extensions, sizeof(app->extensions));
    else app->extensions[0] = '\0';

    snprintf(query, sizeof(query), "[%d].size", index);
    s = JSON_SearchT(buf, len, query, strlen(query), &val, &vallen, &type);
    app->size = (s == JSONSuccess) ? atoi(val) : 0;

    snprintf(app->elf_url, sizeof(app->elf_url), "%s%s/program.elf", ELF_BASE, app->id);

    free(buf);
    return 0;
}

static int load_catalog_from_disk(void) {
    catalog_app_t app;
    app_count = 0;
    for (int i = 0; i < MAX_APPS; i++) {
        if (catalog_read_one(i, &app) != 0) break;
        strncpy(app_ids[app_count], app.id, sizeof(app_ids[0]) - 1);
        strncpy(app_names[app_count], app.name, sizeof(app_names[0]) - 1);
        strncpy(app_requires[app_count], app.requires, sizeof(app_requires[0]) - 1);
        strncpy(app_version[app_count], app.version, sizeof(app_version[0]) - 1);
        app_sizes[app_count] = app.size;
        app_ids[app_count][sizeof(app_ids[0]) - 1] = '\0';
        app_names[app_count][sizeof(app_names[0]) - 1] = '\0';
        app_requires[app_count][sizeof(app_requires[0]) - 1] = '\0';
        app_version[app_count][sizeof(app_version[0]) - 1] = '\0';
        app_count++;
    }

    printf("[appstore] parsed %d apps\n", app_count);
    for (int i = 0; i < app_count; i++) {
        statuses[i] = check_app_status_at(i);
    }
    return app_count;
}

static int load_catalog(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(0, 0, "Checking catalog cache...", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    text_mode_flush();

    if (is_catalog_fresh()) {
        text_mode_print_at_attr(0, 1, "Loading cached catalog...", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_flush();
        int result = load_catalog_from_disk();
        if (result > 0) return result;
    }

    printf("[appstore] Catalog missing or stale, requesting OS download\n");
    text_mode_print_at_attr(0, 1, "Requesting OS download...", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_flush();
    if (!os_download_via_os(CATALOG_URL, CATALOG_PATH, 0)) {
        printf("[appstore] Failed to queue OS download\n");
        return -1;
    }

    printf("[appstore] OS download requested\n");
    return -2;
}

static void show_pending_install_result(void) {
    int result = appcfg_get_int("os/download_result", 0);
    if (result == 0) return;

    char installing_id[48];
    appcfg_get_string(INSTALLING_KEY, "", installing_id, sizeof(installing_id));
    config_delete(INSTALLING_KEY);
    config_delete("os/download_path");

    if (result > 0 && installing_id[0]) {
        int app_idx = -1;
        for (int i = 0; i < app_count; i++) {
            if (strcmp(app_ids[i], installing_id) == 0) {
                app_idx = i;
                break;
            }
        }

        if (app_idx >= 0) {
            catalog_app_t *app = malloc(sizeof(catalog_app_t));
            if (app) {
                catalog_read_one(app_idx, app);
                char path[256];
                snprintf(path, sizeof(path), "/sdcard/apps/%s/manifest.cfg", installing_id);
                FILE *f = fopen(path, "w");
                if (f) {
                    char line[512];
                    int n = snprintf(line, sizeof(line), "name=%s\nlauncher=yes\n", app->name);
                    if (app->version[0]) n += snprintf(line + n, sizeof(line) - n, "version=%s\n", app->version);
                    if (app->short_description[0]) n += snprintf(line + n, sizeof(line) - n, "short_description=%s\n", app->short_description);
                    if (app->long_description[0]) n += snprintf(line + n, sizeof(line) - n, "long_description=%s\n", app->long_description);
                    if (app->homepage[0]) n += snprintf(line + n, sizeof(line) - n, "homepage=%s\n", app->homepage);
                    if (app->extensions[0]) n += snprintf(line + n, sizeof(line) - n, "extensions=%s\n", app->extensions);
                    if (app->requires[0]) n += snprintf(line + n, sizeof(line) - n, "requires=%s\n", app->requires);
                    fwrite(line, 1, n, f);
                    fclose(f);
                    printf("[appstore] Manifest written for %s\n", installing_id);
                    config_delete("os/download_result");
                    ui2_screen_toast_show(screen, "Installed!", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
                    free(app);
                    return;
                }
                free(app);
            }
        }
        ui2_screen_toast_show(screen, "Install failed", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
    } else {
        char err[32];
        snprintf(err, sizeof(err), "Download failed (%d)", result);
        ui2_screen_toast_show(screen, err, TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
    }
}

static void rebuild_catalog_ui(void) {
    for (int i = 0; i < app_count; i++) {
        char status_char;
        switch (statuses[i]) {
            case STATUS_NEW: status_char = '+'; break;
            case STATUS_INSTALLED: status_char = ' '; break;
            case STATUS_UPDATE: status_char = '~'; break;
            default: status_char = '?'; break;
        }

        int n = snprintf(list_texts[i], DISPLAY_LEN, "%c %s", status_char, app_names[i]);
        if (n >= DISPLAY_LEN) list_texts[i][DISPLAY_LEN - 1] = '\0';
        list_items[i] = list_texts[i];

        if (statuses[i] == STATUS_NEW || statuses[i] == STATUS_UPDATE) {
            row_attrs[i] = TEXT_ATTR_BOLD;
        } else {
            row_attrs[i] = TEXT_ATTR_NORMAL;
        }
    }

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int btn_h = 3;

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_VERTICAL);
    ui2_layout_set_gap(root, 1);

    ui2_label_t *title = ui2_label_create(0, 0, "App Store",
                                           TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui2_layout_add(root, UI2_WIDGET(title));

    app_list = ui2_list_create(0, 0, cols, rows - btn_h - 3);
    ui2_list_set_title(app_list, "Available Apps");
    ui2_list_set_colors(app_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                        TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLUE, TEXT_COLOR_CYAN);
    ui2_list_set_border(app_list, true);
    ui2_list_set_items(app_list, (const char **)list_items, app_count);
    ui2_list_set_row_attrs(app_list, row_attrs, app_count);
    ui2_list_set_callbacks(app_list, NULL, on_app_activated, NULL);
    ui2_layout_add(root, UI2_WIDGET(app_list));

    ui2_layout_t *btn_row = ui2_layout_create(0, 0, cols, btn_h, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(btn_row, 1);

    int btn_w = 3;
    ui2_button_t *btn_up = ui2_button_create(0, 0, btn_w, btn_h, ICON_ARROW_BIG_UP);
    ui2_button_set_callback(btn_up, on_up_click, NULL);

    ui2_button_t *btn_down = ui2_button_create(0, 0, btn_w, btn_h, ICON_ARROW_BIG_DOWN);
    ui2_button_set_callback(btn_down, on_down_click, NULL);

    ui2_button_t *btn_action = ui2_button_create(0, 0, btn_w, btn_h, ICON_ARROW_DOWN_TO_LINE);
    ui2_button_set_callback(btn_action, on_action_click, NULL);

    ui2_button_t *btn_exit = ui2_button_create(0, 0, btn_w, btn_h, ICON_X);
    ui2_button_set_callback(btn_exit, on_exit_click, NULL);

    ui2_layout_add(btn_row, UI2_WIDGET(btn_up));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_down));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_action));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_exit));

    ui2_layout_add(root, UI2_WIDGET(btn_row));

    ui2_screen_set_root(screen, root);
    ui2_screen_focus_set(screen, UI2_WIDGET(app_list));
}

static int compute_wrapped_lines(const char *text, int width) {
    if (!text || !*text) return 0;
    const char *p = text;
    int lines = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        int len = 0, last_space = -1;
        while (p[len] && len < width) {
            if (p[len] == ' ') last_space = len;
            len++;
        }
        if (len >= width && last_space > 0) len = last_space;
        lines++;
        p += len;
        while (*p == ' ') p++;
    }
    return lines > 0 ? lines : 1;
}

static void do_install(int index) {
    if (index < 0 || index >= app_count) return;
    app_status_t status = check_app_status_at(index);
    if (status != STATUS_NEW && status != STATUS_UPDATE) return;

    catalog_app_t *app = malloc(sizeof(catalog_app_t));
    if (!app) { go_back_to_catalog(); return; }
    if (catalog_read_one(index, app) != 0) { free(app); go_back_to_catalog(); return; }

    mode = MODE_DOWNLOADING;
    config_set_string(INSTALLING_KEY, app->id);
    config_set_int("os/download_result", 0);
    snprintf(download_sd_path, sizeof(download_sd_path), "%s/%s/program.elf", SD_APPS_DIR, app->id);
    mkdir(SD_APPS_DIR, 0755);
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", SD_APPS_DIR, app->id);
    mkdir(dir_path, 0755);
    if (!os_download_via_os(app->elf_url, download_sd_path, (size_t)app->size)) {
        ui2_screen_toast_show(screen, "Queue failed", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
        go_back_to_catalog();
    }
    free(app);
}

static void do_uninstall(int index) {
    if (index < 0 || index >= app_count) return;
    char app_path[256];
    snprintf(app_path, sizeof(app_path), "%s/%s", SD_APPS_DIR, app_ids[index]);
    delete_dir_contents(app_path);
    remove(app_path);
}

static void go_back_to_catalog(void) {
    mode = MODE_CATALOG;
    for (int i = 0; i < app_count; i++) {
        statuses[i] = check_app_status_at(i);
    }
    rebuild_catalog_ui();
    ui2_screen_render(screen);
}

static void show_confirm_uninstall(int index) {
    mode = MODE_CONFIRM_UNINSTALL;
    uninstall_index = index;
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int btn_h = 3;

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);

    int cy = rows / 2 - 4;
    ui2_label_t *title = ui2_label_create(0, cy, "Uninstall?", TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
    ui2_layout_add(root, UI2_WIDGET(title));

    cy += 2;
    int h = compute_wrapped_lines(app_names[index], cols);
    ui2_text_t *name = ui2_text_create(0, cy, cols, h);
    ui2_text_set_content(name, app_names[index], TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_NORMAL);
    ui2_layout_add(root, UI2_WIDGET(name));
    cy += h;

    cy += 2;
    ui2_label_t *msg = ui2_label_create(0, cy, "Remove app and all its data.",
                                        TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    ui2_layout_add(root, UI2_WIDGET(msg));

    ui2_layout_t *btn_row = ui2_layout_create(0, rows - btn_h, cols, btn_h, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(btn_row, 1);

    ui2_button_t *btn_yes = ui2_button_create(0, 0, 10, btn_h, ICON_TRASH_2 " Yes");
    ui2_button_set_callback(btn_yes, on_confirm_yes_click, NULL);
    ui2_layout_add(btn_row, UI2_WIDGET(btn_yes));

    ui2_button_t *btn_no = ui2_button_create(0, 0, 8, btn_h, " No");
    ui2_button_set_callback(btn_no, on_confirm_no_click, NULL);
    ui2_layout_add(btn_row, UI2_WIDGET(btn_no));

    ui2_layout_add(root, UI2_WIDGET(btn_row));
    ui2_screen_set_root(screen, root);
}

static void on_confirm_yes_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    do_uninstall(uninstall_index);
    uninstall_index = -1;
    ui2_screen_toast_show(screen, "Uninstalled", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
    go_back_to_catalog();
}

static void on_confirm_no_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    uninstall_index = -1;
    go_back_to_catalog();
}

static void on_install_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    do_install(detail_index);
}

static void on_uninstall_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    show_confirm_uninstall(detail_index);
}

static void on_back_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    go_back_to_catalog();
}

static void show_detail(int index) {
    catalog_app_t *app = malloc(sizeof(catalog_app_t));
    if (!app) return;
    if (catalog_read_one(index, app) != 0) { free(app); return; }

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int btn_h = 3;

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);

    int y = 0;
    int h = compute_wrapped_lines(app->name, cols);
    ui2_text_t *title = ui2_text_create(0, y, cols, h);
    ui2_text_set_content(title, app->name, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
    ui2_layout_add(root, UI2_WIDGET(title));
    y += h;

    if (y < 2) y = 2;
    if (app->short_description[0]) {
        h = compute_wrapped_lines(app->short_description, cols);
        ui2_text_t *desc = ui2_text_create(0, y, cols, h);
        ui2_text_set_content(desc, app->short_description, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(desc));
        y += h;
    }

    char line[64];
    if (app->version[0]) {
        snprintf(line, sizeof(line), "Version: %s", app->version);
        ui2_label_t *lbl = ui2_label_create(0, y, line, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(lbl));
        y++;
    }

    if (app->size > 0) {
        snprintf(line, sizeof(line), "Size: %d KB", app->size / 1024);
        ui2_label_t *lbl = ui2_label_create(0, y, line, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(lbl));
        y++;
    }

    if (app->requires[0]) {
        snprintf(line, sizeof(line), "Requires: %s", app->requires);
        ui2_label_t *lbl = ui2_label_create(0, y, line, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(lbl));
        y++;
    }

    if (app->homepage[0]) {
        h = compute_wrapped_lines(app->homepage, cols);
        ui2_text_t *home = ui2_text_create(0, y, cols, h);
        ui2_text_set_content(home, app->homepage, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(home));
        y += h;
    }

    if (has_missing_caps(index)) {
        ui2_label_t *warn = ui2_label_create(0, y, "Missing required hardware",
                                             TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
        ui2_layout_add(root, UI2_WIDGET(warn));
    }

    app_status_t status = check_app_status_at(index);
    ui2_layout_t *btn_row = ui2_layout_create(0, rows - btn_h, cols, btn_h, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(btn_row, 1);

    if (status == STATUS_NEW || status == STATUS_UPDATE) {
        const char *label = (status == STATUS_UPDATE)
            ? ICON_DOWNLOAD " Update" : ICON_DOWNLOAD " Install";
        ui2_button_t *btn = ui2_button_create(0, 0, 14, btn_h, label);
        ui2_button_set_callback(btn, on_install_click, NULL);
        ui2_layout_add(btn_row, UI2_WIDGET(btn));
    } else if (status == STATUS_INSTALLED) {
        ui2_button_t *btn = ui2_button_create(0, 0, 14, btn_h, ICON_TRASH_2 " Uninstall");
        ui2_button_set_callback(btn, on_uninstall_click, NULL);
        ui2_layout_add(btn_row, UI2_WIDGET(btn));
    }

    ui2_button_t *btn_back = ui2_button_create(0, 0, 10, btn_h, ICON_ARROW_BIG_LEFT " Back");
    ui2_button_set_callback(btn_back, on_back_click, NULL);
    ui2_layout_add(btn_row, UI2_WIDGET(btn_back));

    ui2_layout_add(root, UI2_WIDGET(btn_row));

    ui2_screen_set_root(screen, root);

    free(app);
}

static void on_app_activated(int index, void *data) {
    (void)data;
    if (index < 0 || index >= app_count) return;
    mode = MODE_DETAIL;
    detail_index = index;
    show_detail(index);
}

static void on_up_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    if (!app_list) return;
    int sel = ui2_list_get_selection(app_list);
    if (sel > 0) {
        ui2_list_set_selection(app_list, sel - 1);
        ui2_screen_render(screen);
    }
}

static void on_down_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    if (!app_list) return;
    int sel = ui2_list_get_selection(app_list);
    if (sel < app_count - 1) {
        ui2_list_set_selection(app_list, sel + 1);
        ui2_screen_render(screen);
    }
}

static void on_action_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    if (!app_list) return;
    int sel = ui2_list_get_selection(app_list);
    if (sel >= 0 && sel < app_count) {
        on_app_activated(sel, NULL);
    }
}

static void on_exit_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    os_exit();
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH | EVENT_TIMER;
    ctx->timer_interval_ms = 250;
    text_mode_init();

    list_texts = malloc(MAX_APPS * DISPLAY_LEN);
    if (!list_texts) {
        printf("[appstore] ERROR: failed to allocate memory\n");
        return;
    }

    screen = ui2_screen_create();
    mode = MODE_CATALOG;
    app_count = 0;
    ignore_events = 0;
    uninstall_index = -1;

    show_pending_install_result();

    int n = load_catalog();
    if (n > 0) {
        rebuild_catalog_ui();
    } else if (n == -2) {
        mode = MODE_ERROR;
        snprintf(error_msg, sizeof(error_msg), "Downloading catalog...");
        text_mode_clear(TEXT_COLOR_BLACK);
        text_mode_print_at_attr(0, 2, error_msg, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_flush();
        return;
    } else {
        mode = MODE_ERROR;
        snprintf(error_msg, sizeof(error_msg), "Failed to load catalog (%d)", n);
        text_mode_clear(TEXT_COLOR_BLACK);
        text_mode_print_at_attr(0, 1, error_msg, TEXT_COLOR_RED, TEXT_ATTR_NORMAL);
        text_mode_print_at_attr(0, 2, "Check WiFi and try again", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
        text_mode_print_at_attr(0, 3, "Press any key to exit", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_flush();
        return;
    }

    ui2_screen_render(screen);
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;
    if (ignore_events > 0) {
        ignore_events--;
        return;
    }
    if (event->type == EVENT_TIMER) {
        if (ui2_screen_toast_active(screen)) {
            ui2_screen_toast_tick(screen);
            if (!ui2_screen_toast_active(screen)) {
                ui2_screen_render(screen);
            }
        }
        return;
    }
    if (mode == MODE_ERROR) {
        if (event->type == EVENT_KEYBOARD || event->type == EVENT_TOUCH) {
            os_exit();
        }
        return;
    }
    if (mode == MODE_DOWNLOADING) return;

    if (mode == MODE_DETAIL) {
        if (ui2_screen_handle_event(screen, event)) {
            ui2_screen_render(screen);
            return;
        }
        if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
            char key = event->keyboard.key;
            if (key == 'y' || key == 'Y') {
                do_install(detail_index);
            } else if (key == 'u' || key == 'U') {
                show_confirm_uninstall(detail_index);
            } else if (key == 27) {
                go_back_to_catalog();
            }
        }
        return;
    }

    if (mode == MODE_CONFIRM_UNINSTALL) {
        if (ui2_screen_handle_event(screen, event)) {
            ui2_screen_render(screen);
            return;
        }
        if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
            char key = event->keyboard.key;
            if (key == 'y' || key == 'Y') {
                on_confirm_yes_click(NULL, NULL);
            } else if (key == 'n' || key == 'N' || key == 27) {
                on_confirm_no_click(NULL, NULL);
            }
        }
        return;
    }

    if (ui2_screen_handle_event(screen, event))
        ui2_screen_render(screen);
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    free(list_texts);
    list_texts = NULL;
    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
    }
    text_mode_clear(TEXT_COLOR_BLACK);
}
