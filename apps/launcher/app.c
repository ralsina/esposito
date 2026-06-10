#include "os_core.h"
#include "text_mode.h"
#include "hardware.h"
#include "ui2.h"
#include "lucide_icons.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "launcher";

#define APP_LOADER_MAX_APPS 32

extern int app_loader_scan(char (*app_names)[256], int max_apps);
extern bool app_manifest_get_display_name(const char *app_name, char *display_name, size_t max_len);

static int app_launcher_selected = 0;
static int app_count = 0;
static char app_names[APP_LOADER_MAX_APPS][256];
static char app_display_names[APP_LOADER_MAX_APPS][256];

static ui2_screen_t *screen = NULL;
static ui2_list_t *launcher_list = NULL;

static void sort_app_names(void) {
    for (int i = 0; i < app_count - 1; i++) {
        for (int j = i + 1; j < app_count; j++) {
            if (strcmp(app_names[i], app_names[j]) > 0) {
                char tmp[64];
                strcpy(tmp, app_names[i]);
                strcpy(app_names[i], app_names[j]);
                strcpy(app_names[j], tmp);
            }
        }
    }
}

static void on_selection_changed(int new_selection, void *user_data) {
    (void)user_data;
    app_launcher_selected = new_selection;
}

static void on_item_activated(int item_index, void *user_data) {
    (void)user_data;
    if (app_count > 0 && item_index >= 0 && item_index < app_count) {
        os_log(TAG, "Launching app: %s", app_names[item_index]);
        os_load_app(app_names[item_index]);
    }
}

static void on_up_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (launcher_list && app_count > 0) {
        int new_selection = (app_launcher_selected - 1 + app_count) % app_count;
        app_launcher_selected = new_selection;
        ui2_list_set_selection(launcher_list, new_selection);
    }
}

static void on_open_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (app_count > 0) {
        os_load_app(app_names[app_launcher_selected]);
    }
}

static void on_down_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (launcher_list && app_count > 0) {
        int new_selection = (app_launcher_selected + 1) % app_count;
        app_launcher_selected = new_selection;
        ui2_list_set_selection(launcher_list, new_selection);
    }
}

static void build_launcher_screen(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    screen = ui2_screen_create();
    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_screen_set_root(screen, root);

    if (app_count > 0) {
        int list_height = rows - 6;
        launcher_list = ui2_list_create(1, 1, cols - 2, list_height);
        ui2_list_set_title(launcher_list, ICON_HOME " App Launcher");
        ui2_list_set_colors(launcher_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                            TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);

        static const char *display_ptrs[APP_LOADER_MAX_APPS];
        for (int i = 0; i < app_count; i++) {
            display_ptrs[i] = app_display_names[i];
        }
        ui2_list_set_items(launcher_list, display_ptrs, app_count);
        ui2_list_set_selection(launcher_list, app_launcher_selected);
        ui2_list_set_callbacks(launcher_list, on_selection_changed, on_item_activated, NULL);
        ui2_layout_add(root, UI2_WIDGET(launcher_list));
        ui2_screen_focus_set(screen, UI2_WIDGET(launcher_list));
    } else {
        ui2_label_t *msg = ui2_label_create(cols / 2 - 10, rows / 2, "No apps available!",
                                             TEXT_COLOR_RED, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(msg));
    }

    int btn_row = rows - 4;
    ui2_layout_t *bar = ui2_layout_create(1, btn_row, cols - 2, 3, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(bar, 2);
    ui2_layout_add(root, UI2_WIDGET(bar));

    ui2_button_t *up = ui2_button_create(0, 0, 3, 3, ICON_ARROW_BIG_UP);
    ui2_button_set_callback(up, on_up_click, NULL);
    ui2_layout_add(bar, UI2_WIDGET(up));

    ui2_button_t *open = ui2_button_create(0, 0, 3, 3, ICON_CHECK);
    ui2_button_set_callback(open, on_open_click, NULL);
    ui2_layout_add(bar, UI2_WIDGET(open));

    ui2_button_t *down = ui2_button_create(0, 0, 3, 3, ICON_ARROW_BIG_DOWN);
    ui2_button_set_callback(down, on_down_click, NULL);
    ui2_layout_add(bar, UI2_WIDGET(down));
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;

    os_set_cpu_freq_mhz(160);
    text_mode_init();

    app_count = app_loader_scan(app_names, APP_LOADER_MAX_APPS);
    sort_app_names();
    for (int i = 0; i < app_count; i++) {
        app_manifest_get_display_name(app_names[i], app_display_names[i], 64);
    }

    app_context_t *current = os_get_current_app();
    app_launcher_selected = 0;
    if (current) {
        for (int i = 0; i < app_count; i++) {
            if (strcmp(app_names[i], current->name) == 0) {
                app_launcher_selected = i;
                break;
            }
        }
    }

    build_launcher_screen();
    ui2_screen_render(screen);
    os_log(TAG, "App launcher initialized with %d apps", app_count);
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;
    if (ui2_screen_handle_event(screen, event))
        ui2_screen_render(screen);
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
        launcher_list = NULL;
    }
    os_log(TAG, "App launcher closing");
}
