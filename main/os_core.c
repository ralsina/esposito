#include "os_core.h"
#include "app_loader.h"
#include "hardware.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "os_core";

// App launcher/switcher
static bool app_launcher_active = false;
static int app_launcher_selected = 0;

static void app_launcher_show(void) {
    char app_names[4][64];
    int app_count = app_loader_scan(app_names, 4);

    // Clear screen and show launcher
    display_clear(0x001F); // Blue background
    display_draw_text(5, 5, "App Launcher", 0xFFFF); // White title
    display_draw_text(5, 25, "Use +/- to select", 0xFFE0); // Yellow instructions
    display_draw_text(5, 45, "Enter to launch", 0xFFE0); // Yellow instructions

    // Show available apps
    int y = 75;
    for (int i = 0; i < app_count; i++) {
        uint16_t color = (i == app_launcher_selected) ? 0x07E0 : 0xFFFF; // Green for selected, white otherwise
        char prefix[] = {(i == app_launcher_selected) ? '>' : ' ', '\0'};
        char buf[80]; // Larger buffer to avoid truncation warning
        snprintf(buf, sizeof(buf), "%s %s", prefix, app_names[i]);
        display_draw_text(10, y, buf, color);
        y += 20;
    }

    ESP_LOGI(TAG, "App launcher shown, %d apps available", app_count);
}

static void app_launcher_handle_key(char key) {
    char app_names[4][64];
    int app_count = app_loader_scan(app_names, 4);

    switch (key) {
        case '+':
        case '=':
            app_launcher_selected = (app_launcher_selected + 1) % app_count;
            app_launcher_show();
            break;

        case '-':
        case '_':
            app_launcher_selected = (app_launcher_selected - 1 + app_count) % app_count;
            app_launcher_show();
            break;

        case '\n':
        case '\r':
            // Launch selected app
            ESP_LOGI(TAG, "Launching app: %s", app_names[app_launcher_selected]);
            app_launcher_active = false;
            os_load_app(app_names[app_launcher_selected]);
            break;

        case 27: // ESC
            // Cancel launcher, return to current app
            ESP_LOGI(TAG, "Launcher cancelled");
            app_launcher_active = false;
            break;

        default:
            // Ignore other keys
            break;
    }
}

#define MAX_EVENTS 32
#define EVENT_QUEUE_SIZE 32

static event_t event_queue[EVENT_QUEUE_SIZE];
static size_t event_queue_head = 0;
static size_t event_queue_tail = 0;
static app_context_t *current_app = NULL;

// Event queue management
static bool event_queue_push(event_t *event) {
    size_t next = (event_queue_head + 1) % EVENT_QUEUE_SIZE;
    if (next == event_queue_tail) {
        ESP_LOGW(TAG, "Event queue full");
        return false;
    }
    event_queue[event_queue_head] = *event;
    event_queue_head = next;
    return true;
}

static bool event_queue_pop(event_t *event) {
    if (event_queue_tail == event_queue_head) {
        return false;
    }
    *event = event_queue[event_queue_tail];
    event_queue_tail = (event_queue_tail + 1) % EVENT_QUEUE_SIZE;
    return true;
}

static bool event_queue_peek(event_t *event) {
    if (event_queue_tail == event_queue_head) {
        return false;
    }
    *event = event_queue[event_queue_tail];
    return true;
}

static size_t event_queue_size(void) {
    if (event_queue_head >= event_queue_tail) {
        return event_queue_head - event_queue_tail;
    }
    return EVENT_QUEUE_SIZE - event_queue_tail + event_queue_head;
}

// Filesystem initialization
bool os_init_filesystem(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS: total: %d, used: %d", total, used);
    }

    return true;
}

// App management
app_context_t *os_get_current_app(void) {
    return current_app;
}

void os_set_current_app(app_context_t *app) {
    current_app = app;
}

void os_unload_app(void) {
    if (current_app) {
        if (current_app->close) {
            current_app->close(current_app);
        }
        if (current_app->handle) {
            // dlclose(current_app->handle); // TODO: implement when dlopen is ready
        }
        free(current_app);
        current_app = NULL;
    }
}

bool os_load_app(const char *app_name) {
    // Checkpoint and close current app
    if (current_app) {
        if (current_app->checkpoint) {
            current_app->checkpoint(current_app);
        }
        os_unload_app();
    }

    ESP_LOGI(TAG, "Loading app: %s", app_name);

    // Use app_loader to load the app
    if (!app_loader_load(app_name)) {
        ESP_LOGE(TAG, "Failed to load app %s via app_loader", app_name);
        return false;
    }

    ESP_LOGI(TAG, "App %s loaded successfully with subscriptions 0x%lX",
             app_name, (unsigned long)current_app->subscriptions);
    return true;
}

// Event posting (called by hardware layer)
void os_post_event(event_t *event) {
    event_queue_push(event);
}

// Main event loop
void os_event_loop(void) {
    ESP_LOGI(TAG, "Starting event loop");

    static int loop_count = 0;

    while (1) {
        event_t event;
        loop_count++;

        // Debug every 100 loops
        if (loop_count % 100 == 0) {
            ESP_LOGI(TAG, "Event loop tick %d, current_app: %s, subscriptions: 0x%X",
                     loop_count,
                     current_app ? current_app->name : "none",
                     current_app ? current_app->subscriptions : 0);
        }

        // Poll for keyboard events if current app is subscribed
        if (current_app && (current_app->subscriptions & EVENT_KEYBOARD)) {
            if (keyboard_read_event(&event)) {
                // Add the keyboard event to the queue
                event_queue_push(&event);
                ESP_LOGI(TAG, "Keyboard event added to queue");
            }
        }

        // Check if we have events in queue
        if (event_queue_pop(&event)) {
            // Check for app launcher trigger (L key)
            if (event.type == EVENT_KEYBOARD && event.keyboard.pressed &&
                (event.keyboard.key == 'l' || event.keyboard.key == 'L')) {
                ESP_LOGI(TAG, "Launcher activated (L key)");
                app_launcher_active = true;
                app_launcher_selected = 0;
                app_launcher_show();
                continue;
            }

            // If launcher is active, handle keys there
            if (app_launcher_active && event.type == EVENT_KEYBOARD && event.keyboard.pressed) {
                app_launcher_handle_key(event.keyboard.key);
                continue;
            }

            // Deliver to current app if subscribed
            if (current_app && (current_app->subscriptions & event.type)) {
                if (current_app->event_fn) {
                    ESP_LOGI(TAG, "Delivering event to app %s", current_app->name);
                    current_app->event_fn(current_app, &event);
                }
            }
        }

        // Small delay to prevent watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
