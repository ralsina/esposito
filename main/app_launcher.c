// Text Mode App Launcher
// Provides a retro-style app selection interface

#include "app_launcher.h"
#include "os_core.h"
#include "app_loader.h"
#include "app_manifest.h"
#include "ui.h"
#include "hardware.h"
#include "text_mode.h"
#include "sd_card.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "app_launcher";

// App launcher state
static bool app_launcher_active = false;
static int app_launcher_selected = 0;
static int app_count = 0;
static char app_names[APP_LOADER_MAX_APPS][64];         // directory names (for loading)
static char app_display_names[APP_LOADER_MAX_APPS][64]; // human-readable (for display)

// App launcher UI layout
#define HEADER_ROW 1
#define APPS_START_ROW 4

// Button widgets
static ui_button_t *btn_up = NULL;
static ui_button_t *btn_open = NULL;
static ui_button_t *btn_down = NULL;

// List widget for app list
static ui_list_widget_t *app_list = NULL;

static int previous_selected = -1;

// Forward declarations
static void app_launcher_show(void);

// List widget callbacks
static void on_app_list_selection_changed(ui_list_widget_t *list, int new_selection) {
    (void)list;
    app_launcher_selected = new_selection;
}

static void on_app_list_item_selected(ui_list_widget_t *list, int item_index) {
    (void)list;
    if (app_count > 0 && item_index >= 0 && item_index < app_count) {
        ESP_LOGI(TAG, "Launching app: %s", app_names[item_index]);
        app_launcher_active = false;
        previous_selected = -1;
        os_load_app(app_names[item_index]);
    }
}

// Button widget callbacks
static void on_launcher_up_click(ui_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (app_list && app_count > 0) {
        int new_selection = (app_launcher_selected - 1 + app_count) % app_count;
        app_launcher_selected = new_selection;
        ui_list_set_selection(app_list, new_selection);
    }
    app_launcher_show();
}

static void on_launcher_open_click(ui_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (app_count > 0) {
        app_launcher_active = false;
        previous_selected = -1;
        os_load_app(app_names[app_launcher_selected]);
    }
}

static void on_launcher_down_click(ui_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (app_list && app_count > 0) {
        int new_selection = (app_launcher_selected + 1) % app_count;
        app_launcher_selected = new_selection;
        ui_list_set_selection(app_list, new_selection);
    }
    app_launcher_show();
}

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

static void app_launcher_show_static(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    // Create or update list widget
    if (!app_list) {
        int list_height = rows - 6; // Leave room for buttons and margins
        app_list = ui_list_create(1, 1, cols - 2, list_height);
        ui_list_set_title(app_list, "App Launcher");
        ui_list_set_colors(app_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                           TEXT_COLOR_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui_list_set_border(app_list, true);
        ui_list_set_scrollbar(app_list, true);
        ui_list_set_callbacks(app_list, on_app_list_selection_changed,
                              on_app_list_item_selected, NULL);
    } else {
        // Update dimensions if screen size changed
        app_list->x = 1;
        app_list->y = 1;
        app_list->width = cols - 2;
        app_list->height = rows - 6;
    }

    // Update list items if apps are available
    if (app_count > 0) {
        // Create persistent array of display name pointers
        static const char *display_ptrs[APP_LOADER_MAX_APPS];
        for (int i = 0; i < app_count; i++) {
            display_ptrs[i] = app_display_names[i];
        }

        ui_list_set_items(app_list, display_ptrs, app_count);
        ui_list_set_selection(app_list, app_launcher_selected);
        ui_list_draw(app_list);
    }

    // Create button widgets if they don't exist
    if (!btn_up || !btn_open || !btn_down) {
        int btn_h = 3;  // 3 rows tall
        int btn_row = rows - btn_h - 1;  // Position at bottom
        int btn_w = 13;
        int gap = 2;

        // Calculate total width and center the buttons
        int total_width = (btn_w * 3) + (gap * 2);
        int start_x = (cols - total_width) / 2;

        int btn_up_x = start_x;
        int btn_open_x = btn_up_x + btn_w + gap;
        int btn_down_x = btn_open_x + btn_w + gap;

        btn_up = ui_button_create(btn_up_x, btn_row, btn_w, btn_h, "  UP  ");
        ui_button_set_callback(btn_up, on_launcher_up_click, NULL);

        btn_open = ui_button_create(btn_open_x, btn_row, btn_w, btn_h, " OPEN ");
        ui_button_set_callback(btn_open, on_launcher_open_click, NULL);

        btn_down = ui_button_create(btn_down_x, btn_row, btn_w, btn_h, " DOWN ");
        ui_button_set_callback(btn_down, on_launcher_down_click, NULL);
    }

    // Draw buttons
    ui_button_draw(btn_up);
    ui_button_draw(btn_open);
    ui_button_draw(btn_down);
}

static void app_launcher_show(void) {
    bool first_render = (previous_selected == -1);

    if (first_render) {
        ui_clear();
        app_launcher_show_static();
    } else {
        // Only redraw the list if selection changed
        if (previous_selected != app_launcher_selected && app_list) {
            ui_list_draw(app_list);
            text_mode_flush();
        }
    }

    previous_selected = app_launcher_selected;
}

static void app_launcher_handle_key(char key) {
    int old_selection = app_launcher_selected;

    switch (key) {
        case 'w':
        case 'W':
        case 'A': // Up arrow (some keyboards send different codes)
            // Move up (previous app)
            if (app_list && app_count > 0) {
                int new_selection = (app_launcher_selected - 1 + app_count) % app_count;
                app_launcher_selected = new_selection;
                ui_list_set_selection(app_list, new_selection);
                app_launcher_show();
            }
            break;

        case 's':
        case 'S':
        case 'B': // Down arrow (some keyboards send different codes)
            // Move down (next app)
            if (app_list && app_count > 0) {
                int new_selection = (app_launcher_selected + 1) % app_count;
                app_launcher_selected = new_selection;
                ui_list_set_selection(app_list, new_selection);
                app_launcher_show();
            }
            break;

        case '\n':
        case '\r':
            // Launch selected app
            ESP_LOGI(TAG, "Launching app: %s", app_names[app_launcher_selected]);
            app_launcher_active = false;
            previous_selected = -1; // Reset for next launch
            os_load_app(app_names[app_launcher_selected]);
            return;

        default:
            // Ignore other keys
            return;
    }

    // Only update if selection changed
    if (old_selection != app_launcher_selected) {
        app_launcher_show();
    }
}

// Public API

void app_launcher_start(void) {
    ESP_LOGI(TAG, "Starting app launcher");

    if (!sd_card_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted!");
        app_count = 0;
        app_launcher_active = true;
        previous_selected = -1;
        ui_clear();
        ui_label_attr((TEXT_MODE_COLS - 20) / 2, 10, "No SD card detected!", TEXT_COLOR_RED, TEXT_ATTR_BOLD);
        ui_label((TEXT_MODE_COLS - 34) / 2, 12, "Insert a SD card with apps and reset", TEXT_COLOR_WHITE);
        return;
    }

    // Get list of available apps (manifest-filtered)
    app_count = app_loader_scan(app_names, APP_LOADER_MAX_APPS);
    sort_app_names();
    // Populate human-readable display names from manifests
    for (int i = 0; i < app_count; i++) {
        app_manifest_get_display_name(app_names[i], app_display_names[i], 64);
    }
    if (app_count == 0) {
        ESP_LOGE(TAG, "No apps found!");
        ui_clear();
        ui_label(5, 5, "No apps available!", TEXT_COLOR_RED);
        return;
    }

    // Start with current app selected, if found
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
    previous_selected = -1; // Force first render
    app_launcher_active = true;

    // Show launcher
    app_launcher_show();
}

bool app_launcher_is_active(void) {
    return app_launcher_active;
}

void app_launcher_handle_event(event_t *event) {
    if (!app_launcher_active) return;

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        app_launcher_handle_key(event->keyboard.key);
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        // Convert pixel coordinates to character coordinates
        int cw = text_mode_get_char_width();
        int ch = text_mode_get_char_height();
        int x_col = event->touch.x / cw;
        int y_col = event->touch.y / ch;

        // Create a modified touch event with character coordinates
        event_t char_event = *event;
        char_event.touch.x = x_col;
        char_event.touch.y = y_col;

        // Try list widget first
        if (app_list && ui_list_handle_touch(app_list, &char_event)) {
            return; // List widget handled the touch
        }

        // Try button widgets
        if (btn_up && ui_button_handle_touch(btn_up, &char_event)) return;
        if (btn_open && ui_button_handle_touch(btn_open, &char_event)) return;
        if (btn_down && ui_button_handle_touch(btn_down, &char_event)) return;
    }
}