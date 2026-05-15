// Text Mode App Launcher
// Provides a retro-style app selection interface

#include "app_launcher.h"
#include "os_core.h"
#include "app_loader.h"
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
static char app_names[APP_LOADER_MAX_APPS][64];

// App launcher UI layout
#define HEADER_ROW 1
#define APPS_START_ROW 4

// Button positions (set on first render)
static int btn_up_x = 0;
static int btn_open_x = 0;
static int btn_down_x = 0;
static int btn_row = 0;
static int btn_w = 0;

static int previous_selected = -1;

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

    ui_label_attr((cols - 20) / 2, HEADER_ROW, "Esposito OS App Launcher", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    ui_separator(HEADER_ROW + 1);

    btn_w = 13;
    int gap = 2;
    btn_row = rows - 3;
    btn_up_x = 2;
    btn_open_x = btn_up_x + btn_w + gap;
    btn_down_x = btn_open_x + btn_w + gap;

    char spacer[64];
    memset(spacer, ' ', btn_w);
    spacer[btn_w] = '\0';

    text_mode_print_at_attr_bg(btn_up_x, btn_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(btn_up_x + (btn_w - 6) / 2, btn_row, "  UP  ", TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(btn_open_x, btn_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(btn_open_x + (btn_w - 6) / 2, btn_row, " OPEN ", TEXT_COLOR_BLACK, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(btn_down_x, btn_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(btn_down_x + (btn_w - 6) / 2, btn_row, " DOWN ", TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
}

static void app_launcher_show(void) {
    bool first_render = (previous_selected == -1);

    if (first_render) {
        ui_clear();
        app_launcher_show_static();
    }

    int y = APPS_START_ROW;
    for (int i = 0; i < app_count; i++) {
        if (first_render || i == previous_selected || i == app_launcher_selected) {
            uint16_t color = (i == app_launcher_selected) ? TEXT_COLOR_GREEN : TEXT_COLOR_WHITE;
            char marker = (i == app_launcher_selected) ? '>' : ' ';
            char line[80];
            snprintf(line, sizeof(line), "%c %d. %.63s", marker, i + 1, app_names[i]);
            ui_label(5, y, line, color);
        }
        y++;
    }

    previous_selected = app_launcher_selected;
    ESP_LOGI(TAG, "App launcher updated, selected: %d", app_launcher_selected);
}

static void app_launcher_handle_key(char key) {
    int old_selection = app_launcher_selected;

    switch (key) {
        case 'w':
        case 'W':
        case 'A': // Up arrow (some keyboards send different codes)
            // Move up (previous app)
            app_launcher_selected = (app_launcher_selected - 1 + app_count) % app_count;
            break;

        case 's':
        case 'S':
        case 'B': // Down arrow (some keyboards send different codes)
            // Move down (next app)
            app_launcher_selected = (app_launcher_selected + 1) % app_count;
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

    // Get list of available apps
    app_count = app_loader_scan(app_names, APP_LOADER_MAX_APPS);
    sort_app_names();
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
    } else if (event->type == EVENT_TOUCH && event->touch.pressed && btn_w > 0) {
        int cw = text_mode_get_char_width();
        int ch = text_mode_get_char_height();
        if (event->touch.y >= btn_row * ch && event->touch.y < (btn_row + 1) * ch) {
            int x_col = event->touch.x / cw;
            if (x_col >= btn_up_x && x_col < btn_up_x + btn_w) {
                int old = app_launcher_selected;
                app_launcher_selected = (app_launcher_selected - 1 + app_count) % app_count;
                if (old != app_launcher_selected) app_launcher_show();
            } else if (x_col >= btn_open_x && x_col < btn_open_x + btn_w) {
                if (app_count > 0) {
                    ESP_LOGI(TAG, "Launching app: %s", app_names[app_launcher_selected]);
                    app_launcher_active = false;
                    previous_selected = -1;
                    os_load_app(app_names[app_launcher_selected]);
                }
            } else if (x_col >= btn_down_x && x_col < btn_down_x + btn_w) {
                int old = app_launcher_selected;
                app_launcher_selected = (app_launcher_selected + 1) % app_count;
                if (old != app_launcher_selected) app_launcher_show();
            }
        }
    }
}