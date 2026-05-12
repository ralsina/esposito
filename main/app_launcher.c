// Text Mode App Launcher
// Provides a retro-style app selection interface

#include "app_launcher.h"
#include "os_core.h"
#include "app_loader.h"
#include "text_mode.h"
#include "hardware.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "app_launcher";

// App launcher state
static bool app_launcher_active = false;
static int app_launcher_selected = 0;
static int app_count = 0;
static char app_names[10][64]; // Support up to 10 apps

// App launcher UI layout
#define HEADER_ROW 1
#define APPS_START_ROW 4
#define INSTRUCTIONS_START_ROW 15

static int previous_selected = -1; // Track previous selection for smart updates

static void app_launcher_show_static(void) {
    // Draw header (static content)
    text_mode_print_at_color((TEXT_MODE_COLS - 20) / 2, HEADER_ROW, "Esposito OS App Launcher", TEXT_COLOR_CYAN);

    // Draw separator line
    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, HEADER_ROW + 1, "-", TEXT_COLOR_BLUE);
    }

    // Draw separator
    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, INSTRUCTIONS_START_ROW - 1, "-", TEXT_COLOR_BLUE);
    }

    // Show instructions (static)
    text_mode_print_at_color(5, INSTRUCTIONS_START_ROW, "Controls:", TEXT_COLOR_YELLOW);
    text_mode_print_at_color(5, INSTRUCTIONS_START_ROW + 1, "W/S or Up/Down: Navigate", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, INSTRUCTIONS_START_ROW + 2, "Enter: Launch app", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, INSTRUCTIONS_START_ROW + 3, "ESC: Exit launcher", TEXT_COLOR_WHITE);
}

static void app_launcher_show(void) {
    // Check if this is first render
    bool first_render = (previous_selected == -1);

    if (first_render) {
        // Clear screen once
        text_mode_clear(TEXT_COLOR_BLACK);
        // Draw static elements
        app_launcher_show_static();
    }

    // Update app list (smart update - only redraw changed items)
    int y = APPS_START_ROW;
    for (int i = 0; i < app_count; i++) {
        // Only redraw if:
        // 1. First render
        // 2. This is the previously selected item (needs to be unselected)
        // 3. This is the newly selected item (needs to be highlighted)
        if (first_render || i == previous_selected || i == app_launcher_selected) {
            uint16_t color = (i == app_launcher_selected) ? TEXT_COLOR_GREEN : TEXT_COLOR_WHITE;
            char marker = (i == app_launcher_selected) ? '>' : ' ';
            text_mode_printf_at_color(5, y, color, "%c %d. %s", marker, i + 1, app_names[i]);
        }
        y++;
    }

    // Update status line (only if selection changed)
    if (first_render || previous_selected != app_launcher_selected) {
        text_mode_printf_at_color(5, TEXT_MODE_ROWS - 2, TEXT_COLOR_CYAN,
                                  "Apps: %d | Selected: %s", app_count, app_names[app_launcher_selected]);
    }

    // Remember current selection
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

        case 27: // ESC
            // Exit launcher
            ESP_LOGI(TAG, "Launcher exited");
            app_launcher_active = false;
            previous_selected = -1; // Reset for next launch
            text_mode_clear(TEXT_COLOR_BLACK);
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

    // Get list of available apps
    app_count = app_loader_scan(app_names, 10);
    if (app_count == 0) {
        ESP_LOGE(TAG, "No apps found!");
        text_mode_clear(TEXT_COLOR_BLACK);
        text_mode_print_at_color(5, 5, "No apps available!", TEXT_COLOR_RED);
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
    }
}