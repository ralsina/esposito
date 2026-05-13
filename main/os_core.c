#include "os_core.h"
#include "app_loader.h"
#include "app_launcher.h"
#include "elf_loader.h"
#include "checkpoint.h"
#include "hardware.h"
#include "touchscreen.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static const char *TAG = "os_core";

void os_log(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, tag, fmt, args);
    va_end(args);
}

#define MAX_EVENTS 32
#define EVENT_QUEUE_SIZE 32

static event_t event_queue[EVENT_QUEUE_SIZE];
static size_t event_queue_head = 0;
static size_t event_queue_tail = 0;
static app_context_t *current_app = NULL;

// Serial ring buffer: decouples UART reads from event dispatch
#define SERIAL_RING_SIZE 2048
static char serial_ring[SERIAL_RING_SIZE];
static size_t serial_ring_head = 0;
static size_t serial_ring_tail = 0;

static void serial_ring_push(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        size_t next = (serial_ring_head + 1) % SERIAL_RING_SIZE;
        if (next == serial_ring_tail) {
            serial_ring_tail = (serial_ring_tail + 1) % SERIAL_RING_SIZE;
        }
        serial_ring[serial_ring_head] = data[i];
        serial_ring_head = next;
    }
}

static size_t serial_ring_pop(char *buf, size_t max_len) {
    size_t taken = 0;
    while (taken < max_len && serial_ring_tail != serial_ring_head) {
        buf[taken++] = serial_ring[serial_ring_tail];
        serial_ring_tail = (serial_ring_tail + 1) % SERIAL_RING_SIZE;
    }
    return taken;
}

static void serial_ring_clear(void) {
    serial_ring_head = 0;
    serial_ring_tail = 0;
}

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
            elf_loader_unload((elf_handle_t *)current_app->handle);
        }
        free(current_app);
        current_app = NULL;
    }
    // Deinit serial (teardown driver, clear ring buffer)
    serial_deinit();
    serial_ring_clear();
}

bool os_load_app(const char *app_name) {
    // Checkpoint and close current app
    if (current_app) {
        if (current_app->checkpoint) {
            current_app->checkpoint(current_app);
        }
        checkpoint_save();
        checkpoint_close();
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

        // Poll for keyboard events if current app is subscribed or launcher is active
        if ((current_app && (current_app->subscriptions & EVENT_KEYBOARD)) || app_launcher_is_active()) {
            if (keyboard_read_event(&event)) {
                // Add the keyboard event to the queue
                event_queue_push(&event);
                ESP_LOGI(TAG, "Keyboard event added to queue");
            }
        }

        // Poll for touchscreen events if current app is subscribed
        if (current_app && (current_app->subscriptions & EVENT_TOUCH)) {
            uint16_t x, y;
            bool pressed;
            if (touchscreen_get_position(&x, &y, &pressed)) {
                event.type = EVENT_TOUCH;
                event.touch.x = x;
                event.touch.y = y;
                event.touch.pressed = pressed;
                event_queue_push(&event);
            }
        }

        // Poll serial and dispatch events if current app is subscribed
        if (current_app && (current_app->subscriptions & EVENT_SERIAL)) {
            char buf[64];
            size_t n = serial_read(buf, sizeof(buf));
            if (n > 0) {
                serial_ring_push(buf, n);
            }
            char ev_buf[256];
            size_t taken = serial_ring_pop(ev_buf, sizeof(ev_buf));
            if (taken > 0) {
                event.type = EVENT_SERIAL;
                memcpy(event.serial.data, ev_buf, taken);
                event.serial.len = taken;
                event_queue_push(&event);
            }
        }

        // Check if we have events in queue
        if (event_queue_pop(&event)) {
            // Check for app launcher trigger (Ctrl+ESC)
            if (event.type == EVENT_KEYBOARD && event.keyboard.pressed &&
                event.keyboard.key == 27 &&  // ESC key
                (event.keyboard.modifiers & MODIFIER_CTRL)) {
                ESP_LOGI(TAG, "Launcher activated (Ctrl+ESC)");
                app_launcher_start();
                continue;
            }

            // Check for screenshot trigger (Fn+ESC)
            if (event.type == EVENT_KEYBOARD && event.keyboard.pressed &&
                event.keyboard.key == 27 &&  // ESC key
                (event.keyboard.modifiers & MODIFIER_FN)) {
                ESP_LOGI(TAG, "Screenshot triggered (Fn+ESC)");
                display_save_screenshot();
                continue;
            }

            // If launcher is active, handle keys there
            if (app_launcher_is_active()) {
                app_launcher_handle_event(&event);
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

        // Auto-restart launcher if no app is running
        if (current_app == NULL && !app_launcher_is_active()) {
            app_launcher_start();
        }

        // Small delay to prevent watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
