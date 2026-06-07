#include "os_core.h"
#include "app_heap.h"
#include "app_config.h"
#include "app_loader.h"

#include "app_manifest.h"
#include "elf_loader.h"
#include "hardware.h"
#include "hardware_config.h"
#include "text_mode.h"
#include "terminal_mode.h"
#include "graphics_mode.h"
#include "touchscreen.h"
#include "wifi.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>

static const char *TAG = "os_core";


// Screensaver state
static int64_t screensaver_last_activity = 0;
static bool screensaver_active = false;
static uint8_t screensaver_restore_brightness = 255;
static uint8_t screensaver_restore_kbd_backlight = 255;
static esp_pm_config_t screensaver_saved_pm_config;
static bool screensaver_saved_pm_config_valid = false;

void os_log_global_heap_stats(const char *label) {
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest_8bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG,
             "%s: heap free=%u largest=%u internal_free=%u internal_largest=%u",
             label,
             (unsigned)free_8bit,
             (unsigned)largest_8bit,
             (unsigned)free_internal,
             (unsigned)largest_internal);
}

void os_log(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, tag, fmt, args);
    va_end(args);
}

#define MAX_EVENTS 32
#define EVENT_QUEUE_SIZE 64

static event_t event_queue[EVENT_QUEUE_SIZE];
static size_t event_queue_head = 0;
static size_t event_queue_tail = 0;
static app_context_t *current_app = NULL;
static bool pending_app_switch = false;
static char pending_app_name[64];
static bool in_app_callback = false;

#define OS_STARTUP_FILE_KEY "os/startup_file"
#define OS_SETTINGS_APP_NAME "settings"

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
        os_log_global_heap_stats("before unload");
        app_heap_log_stats("before unload");
        if (current_app->close) {
            current_app->close(current_app);
        }
        if (current_app->handle) {
            elf_loader_unload((elf_handle_t *)current_app->handle);
        }
        free(current_app);
        current_app = NULL;
        config_unbind_app();
        app_heap_reset();
        os_log_global_heap_stats("after unload");
        app_heap_log_stats("after unload");
    }
    // Keep UART0 driver alive across app switches; apps can reconfigure it via serial_init().
    serial_ring_clear();
}

bool os_load_app(const char *app_name) {
    if (in_app_callback) {
        if (!app_name || !app_name[0]) {
            return false;
        }
        strncpy(pending_app_name, app_name, sizeof(pending_app_name) - 1);
        pending_app_name[sizeof(pending_app_name) - 1] = '\0';
        pending_app_switch = true;
        ESP_LOGI(TAG, "Deferring app switch until callback returns: %s", pending_app_name);
        return true;
    }

    os_log_global_heap_stats("before load");
    app_heap_log_stats("before load");

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
    os_log_global_heap_stats("after load");
    app_heap_log_stats("after load");

    // Only save as last app if it should appear in the launcher
    app_sd_manifest_t manifest;
    if (app_manifest_read(app_name, &manifest) && manifest.show_in_launcher) {
        os_settings_set_string("system/last_app", app_name);
    }
    config_bind_app(app_name);
    return true;
}

bool os_open_app_with_file(const char *app_name, const char *file_path) {
    if (!app_name || !app_name[0] || !file_path || !file_path[0]) {
        return false;
    }

    if (!config_bind_app(app_name)) {
        return false;
    }

    bool ok = appcfg_set_string(OS_STARTUP_FILE_KEY, file_path);
    config_unbind_app();
    if (!ok) {
        return false;
    }

    return os_load_app(app_name);
}

bool os_get_time_status(os_time_status_t *status) {
    if (!status) {
        return false;
    }

    memset(status, 0, sizeof(*status));

    time_t now = 0;
    time(&now);
    status->unix_time = (int64_t)now;
    status->synchronized = wifi_time_is_synchronized();
    status->last_sync_time = (int64_t)wifi_time_last_sync();

    struct tm utc_time;
    if (gmtime_r(&now, &utc_time)) {
        status->year = utc_time.tm_year + 1900;
        status->month = utc_time.tm_mon + 1;
        status->day = utc_time.tm_mday;
        status->hour = utc_time.tm_hour;
        status->minute = utc_time.tm_min;
        status->second = utc_time.tm_sec;
        status->weekday = utc_time.tm_wday;
    }

    return true;
}

bool os_time_is_synchronized(void) {
    return wifi_time_is_synchronized();
}

int64_t os_time_last_sync(void) {
    return (int64_t)wifi_time_last_sync();
}

size_t os_settings_get_string(const char *key_path,
                              const char *default_value,
                              char *out,
                              size_t out_size) {
    if (!key_path || !out || out_size == 0) {
        return 0;
    }

    if (!config_bind_app(OS_SETTINGS_APP_NAME)) {
        const char *fallback = default_value ? default_value : "";
        strncpy(out, fallback, out_size - 1);
        out[out_size - 1] = '\0';
        return strlen(out);
    }

    size_t len = appcfg_get_string(key_path, default_value, out, out_size);
    config_unbind_app();
    return len;
}

bool os_settings_set_string(const char *key_path, const char *value) {
    if (!key_path) {
        return false;
    }

    if (!config_bind_app(OS_SETTINGS_APP_NAME)) {
        return false;
    }

    bool ok = appcfg_set_string(key_path, value ? value : "");
    config_unbind_app();
    return ok;
}

int os_settings_get_int(const char *key_path, int default_value) {
    if (!key_path) {
        return default_value;
    }

    if (!config_bind_app(OS_SETTINGS_APP_NAME)) {
        return default_value;
    }

    int value = appcfg_get_int(key_path, default_value);
    config_unbind_app();
    return value;
}

bool os_settings_set_int(const char *key_path, int value) {
    if (!key_path) {
        return false;
    }

    if (!config_bind_app(OS_SETTINGS_APP_NAME)) {
        return false;
    }

    bool ok = appcfg_set_int(key_path, value);
    config_unbind_app();
    return ok;
}

bool os_settings_get_bool(const char *key_path, bool default_value) {
    if (!key_path) {
        return default_value;
    }

    if (!config_bind_app(OS_SETTINGS_APP_NAME)) {
        return default_value;
    }

    bool value = appcfg_get_bool(key_path, default_value);
    config_unbind_app();
    return value;
}

bool os_settings_set_bool(const char *key_path, bool value) {
    if (!key_path) {
        return false;
    }

    if (!config_bind_app(OS_SETTINGS_APP_NAME)) {
        return false;
    }

    bool ok = appcfg_set_bool(key_path, value);
    config_unbind_app();
    return ok;
}

typedef struct {
    char *out;
    size_t out_size;
    size_t len;
    bool truncated;
} os_http_get_ctx_t;

static esp_err_t os_http_get_event_handler(esp_http_client_event_t *event) {
    os_http_get_ctx_t *ctx = (os_http_get_ctx_t *)event->user_data;
    if (!ctx || !ctx->out || ctx->out_size == 0) {
        return ESP_OK;
    }

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data && event->data_len > 0) {
        size_t writable = ctx->out_size - 1 - ctx->len;
        size_t to_copy = (size_t)event->data_len;
        if (to_copy > writable) {
            to_copy = writable;
            ctx->truncated = true;
        }

        if (to_copy > 0) {
            memcpy(ctx->out + ctx->len, event->data, to_copy);
            ctx->len += to_copy;
            ctx->out[ctx->len] = '\0';
        }
    }

    return ESP_OK;
}

int os_http_post(const char *url, const char *post_data, const char *extra_headers[],
                 const char *ca_pem, char *out, size_t out_size, int timeout_ms) {
    if (!url || !post_data || !out || out_size < 2) {
        return -1;
    }

    out[0] = '\0';

    os_http_get_ctx_t ctx = {
        .out = out,
        .out_size = out_size,
        .len = 0,
        .truncated = false,
    };

    size_t free_heap = esp_get_free_heap_size();
    size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "HTTP POST: free_heap=%u, largest=%u, url=%s",
             (unsigned)free_heap, (unsigned)largest_free, url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 5000,
        .event_handler = os_http_get_event_handler,
        .user_data = &ctx,
        .cert_pem = ca_pem,
        .crt_bundle_attach = ca_pem ? NULL : esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP POST: client init failed");
        return -1;
    }

    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (extra_headers) {
        for (int i = 0; extra_headers[i] != NULL && extra_headers[i + 1] != NULL; i += 2) {
            esp_http_client_set_header(client, extra_headers[i], extra_headers[i + 1]);
        }
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        return -status;
    }
    if (ctx.truncated) {
        return -2;
    }
    return (int)ctx.len;
}

int os_http_get(const char *url, char *out, size_t out_size, int timeout_ms) {
    if (!url || !out || out_size < 2) {
        return -1;
    }

    out[0] = '\0';

    os_http_get_ctx_t ctx = {
        .out = out,
        .out_size = out_size,
        .len = 0,
        .truncated = false,
    };

    // Log memory before HTTPS request
    size_t free_heap = esp_get_free_heap_size();
    size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "HTTP: free_heap=%u, largest=%u, url=%s",
             (unsigned)free_heap, (unsigned)largest_free, url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 5000,
        .event_handler = os_http_get_event_handler,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,  // Smaller buffer to save memory
        .buffer_size_tx = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP: client init failed");
        return -1;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        return -status;
    }
    if (ctx.truncated) {
        return -2;
    }
    return (int)ctx.len;
}

size_t os_consume_startup_file(char *out, size_t out_size) {
    if (!out || out_size == 0 || !current_app || !current_app->name[0]) {
        return 0;
    }

    if (!config_bind_app(current_app->name)) {
        out[0] = '\0';
        return 0;
    }

    size_t len = appcfg_get_string(OS_STARTUP_FILE_KEY, "", out, out_size);
    if (len > 0 && out[0]) {
        config_delete(OS_STARTUP_FILE_KEY);
    }
    config_unbind_app();
    return len;
}

// Event posting (called by hardware layer)
void os_post_event(event_t *event) {
    event_queue_push(event);
}

// Process one iteration of event sources (timer, keyboard, etc.)
// and dispatch one queued event if available.
// Called by delay() to allow event processing during tight loops.
void os_process_one_event_iteration(void) {
    extern app_context_t *current_app;

    event_t event;

    // Pop and dispatch one event from the queue
    if (event_queue_pop(&event)) {
        ESP_LOGD(TAG, "Processing event type=%d", event.type);

        // Deliver to current app if subscribed
        if (current_app && (current_app->subscriptions & event.type)) {
            if (current_app->event_fn) {
                ESP_LOGD(TAG, "Delivering event to app %s", current_app->name);
                current_app->event_fn(current_app, &event);
            }
        }
    }
}

// Main event loop
void os_event_loop(void) {
    ESP_LOGI(TAG, "Starting event loop");

    TickType_t timer_last_tick = xTaskGetTickCount();
    app_context_t *timer_last_app = NULL;

    screensaver_last_activity = esp_timer_get_time();

    while (1) {
        event_t event;

        // Generate timer events for apps that opt in.
        if (current_app != timer_last_app) {
            timer_last_app = current_app;
            timer_last_tick = xTaskGetTickCount();
        }

        if (current_app &&
            (current_app->subscriptions & EVENT_TIMER) &&
            current_app->timer_interval_ms > 0) {
            TickType_t now = xTaskGetTickCount();
            TickType_t interval_ticks = pdMS_TO_TICKS(current_app->timer_interval_ms);
            if (interval_ticks == 0) {
                interval_ticks = 1;
            }

            if ((now - timer_last_tick) >= interval_ticks) {
                event.type = EVENT_TIMER;
                if (!event_queue_push(&event)) {
                    timer_last_tick = now;
                } else {
                    timer_last_tick = now;
                }
            }
        }

        // Poll keyboard always so global OS shortcuts cannot be disabled by apps.
        if (keyboard_read_event(&event)) {
            bool wants_keyboard = current_app && (current_app->subscriptions & EVENT_KEYBOARD);
            bool is_ctrl_esc = (event.type == EVENT_KEYBOARD && event.keyboard.pressed &&
                                event.keyboard.key == 27 &&
                                (event.keyboard.modifiers & MODIFIER_CTRL));
            bool is_alt_esc = (event.type == EVENT_KEYBOARD && event.keyboard.pressed &&
                               event.keyboard.key == 27 &&
                               (event.keyboard.modifiers & MODIFIER_ALT));

            if (event.type == EVENT_KEYBOARD) {
                ESP_LOGI(TAG, "KB: key=%d(0x%02x) raw=0x%02x mod=0x%02x pressed=%d wants=%d",
                         event.keyboard.key, event.keyboard.key,
                         event.keyboard.raw_key_code, event.keyboard.modifiers,
                         event.keyboard.pressed, wants_keyboard);
            }

            if (wants_keyboard || is_ctrl_esc || is_alt_esc) {
                event_queue_push(&event);
            }
        }

        // Poll for BOOT button (GPIO 0) to trigger launcher
        {
            static int boot_debounce = 0;
            if (gpio_get_level(BOARD_BOOT_BUTTON_GPIO) == 0) {
                if (boot_debounce < 3) boot_debounce++;
                if (boot_debounce == 3) {
                    boot_debounce = 4;
                    ESP_LOGI(TAG, "BOOT button pressed, triggering launcher");
                    event.type = EVENT_KEYBOARD;
                    event.keyboard.key = 27;
                    event.keyboard.pressed = true;
                    event.keyboard.modifiers = MODIFIER_CTRL;
                    event.keyboard.raw_key_code = 27;
                    event_queue_push(&event);
                }
            } else {
                boot_debounce = 0;
            }
        }

        // Poll for touchscreen events if current app is subscribed or launcher is active
        {
            static bool touch_was_pressed = false;
            static uint16_t last_touch_x = 0;
            static uint16_t last_touch_y = 0;

            bool wants_touch_edge = (current_app && (current_app->subscriptions & EVENT_TOUCH));
            bool wants_touch_stream = (current_app && (current_app->subscriptions & EVENT_TOUCH_CONTINUOUS));

            if (wants_touch_edge || wants_touch_stream) {
                uint16_t x, y;
                bool pressed;
                if (touchscreen_get_position(&x, &y, &pressed)) {
                    last_touch_x = x;
                    last_touch_y = y;

                    if (pressed && !touch_was_pressed && wants_touch_edge) {
                        event.type = EVENT_TOUCH;
                        event.touch.x = x;
                        event.touch.y = y;
                        event.touch.pressed = true;
                        event_queue_push(&event);
                    }

                    if (pressed && wants_touch_stream) {
                        event.type = EVENT_TOUCH_CONTINUOUS;
                        event.touch.x = x;
                        event.touch.y = y;
                        event.touch.pressed = true;
                        event_queue_push(&event);
                    }

                    touch_was_pressed = pressed;
                } else {
                    if (touch_was_pressed && wants_touch_stream) {
                        event.type = EVENT_TOUCH_CONTINUOUS;
                        event.touch.x = last_touch_x;
                        event.touch.y = last_touch_y;
                        event.touch.pressed = false;
                        event_queue_push(&event);
                    }
                    touch_was_pressed = false;
                }
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

        // Drain all pending events from queue
        while (event_queue_pop(&event)) {
            // Track activity on keyboard/touch events
            if (event.type == EVENT_KEYBOARD || event.type == EVENT_TOUCH) {
                screensaver_last_activity = esp_timer_get_time();
            }

            // Screensaver: wake on first keyboard or touch event
            if (screensaver_active) {
                if (event.type == EVENT_KEYBOARD || event.type == EVENT_TOUCH) {
                    display_set_backlight(screensaver_restore_brightness);
                    keyboard_set_backlight(screensaver_restore_kbd_backlight);

                    // Restore saved PM config to bring CPU back to normal frequency
                    int restored_cpu_mhz = 160;
                    if (screensaver_saved_pm_config_valid) {
                        restored_cpu_mhz = screensaver_saved_pm_config.max_freq_mhz;
                        esp_pm_configure(&screensaver_saved_pm_config);
                        screensaver_saved_pm_config_valid = false;
                    }

                    screensaver_active = false;
                    ESP_LOGI(TAG, "Screensaver deactivated (display=%d%%, keyboard backlight=%d%%, CPU=%d MHz)",
                             screensaver_restore_brightness * 100 / 255,
                             screensaver_restore_kbd_backlight * 100 / 255,
                             restored_cpu_mhz);
                    continue;
                }
            }

            // Check for app launcher trigger (Ctrl+ESC)
            if (event.type == EVENT_KEYBOARD && event.keyboard.pressed &&
                event.keyboard.key == 27 &&  // ESC key
                (event.keyboard.modifiers & MODIFIER_CTRL)) {
                ESP_LOGI(TAG, "Launcher activated (Ctrl+ESC)");
                ESP_LOGI(TAG, "ctrl-esc: current_app=%p checkpoint=%p",
                         (void*)current_app,
                         current_app ? (void*)current_app->checkpoint : NULL);
                if (current_app && current_app->checkpoint) {
                    current_app->checkpoint(current_app);
                }
                ESP_LOGI(TAG, "ctrl-esc: checkpoint done");
                {
                    FILE *chk = fopen("/sdcard/apps/lali/config/history", "r");
                    if (chk) {
                        fseek(chk, 0, SEEK_END);
                        long sz = ftell(chk);
                        fclose(chk);
                        ESP_LOGI(TAG, "ctrl-esc: history file exists, size=%ld", sz);
                    } else {
                        ESP_LOGI(TAG, "ctrl-esc: history file NOT FOUND");
                    }
                }
                os_load_app("launcher");
                continue;
            }

            // Check for screenshot trigger (Alt+ESC)
            if (event.type == EVENT_KEYBOARD && event.keyboard.pressed &&
                event.keyboard.key == 27 &&
                (event.keyboard.modifiers & MODIFIER_ALT)) {
                ESP_LOGI(TAG, "Screenshot triggered (Alt+ESC)");
                if (graphics_mode_is_active()) {
                    graphics_mode_save_screenshot();
                } else if (sprite_get_active()) {
                    mkdir("/sdcard/screenshots", 0777);
                    char path[72];
                    int num = 0;
                    FILE *existing;
                    do {
                        snprintf(path, sizeof(path), "/sdcard/screenshots/shot_%03d.ppm", num);
                        existing = fopen(path, "r");
                        if (existing) { fclose(existing); num++; }
                    } while (existing && num < 1000);
                    display_save_screenshot_ppm(path);
                    ESP_LOGI(TAG, "Sprite screenshot saved: %s", path);
                } else if (!terminal_mode_save_screenshot(terminal_mode_default())) {
                    text_mode_save_screenshot();
                }
                continue;
            }

            // Deliver to current app if subscribed
            if (current_app && (current_app->subscriptions & event.type)) {
                if (current_app->event_fn) {
                    ESP_LOGD(TAG, "Delivering event to app %s", current_app->name);
                    in_app_callback = true;
                    current_app->event_fn(current_app, &event);
                    in_app_callback = false;
                }
            }
        }

        // Screensaver: check idle timeout
        {
            static int ss_timeout_min = -1;
            if (ss_timeout_min < 0) {
                ss_timeout_min = os_settings_get_int("system/screensaver_timeout", 5);
            }

            if (ss_timeout_min > 0 && !screensaver_active) {
                int64_t idle_us = esp_timer_get_time() - screensaver_last_activity;
                if (idle_us >= (int64_t)ss_timeout_min * 60 * 1000000LL) {
                    screensaver_restore_brightness = (uint8_t)os_settings_get_int("display/backlight", 255);
                    display_set_backlight(0);

                    screensaver_restore_kbd_backlight = keyboard_get_backlight();
                    keyboard_set_backlight(0);

                    // Save current PM config and reduce CPU to 80 MHz
                    if (esp_pm_get_configuration(&screensaver_saved_pm_config) == ESP_OK) {
                        screensaver_saved_pm_config_valid = true;
                        esp_pm_config_t pm_config = {
                            .max_freq_mhz = 80,
                            .min_freq_mhz = 80,
                            .light_sleep_enable = false,
                        };
                        esp_pm_configure(&pm_config);
                    }

                    screensaver_active = true;
                    ESP_LOGI(TAG, "Screensaver activated after %d min idle (display=0%%, keyboard backlight=0%%, CPU=80 MHz)", ss_timeout_min);
                }
            }
        }

        if (pending_app_switch) {
            char app_to_load[64];
            strncpy(app_to_load, pending_app_name, sizeof(app_to_load) - 1);
            app_to_load[sizeof(app_to_load) - 1] = '\0';
            pending_app_switch = false;

            if (!os_load_app(app_to_load)) {
                ESP_LOGE(TAG, "Deferred app load failed: %s", app_to_load);
            }
            continue;
        }

        // Auto-restart launcher if no app is running
        if (current_app == NULL) {
            os_load_app("launcher");
        }

        // Small delay to prevent watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================================
// Task and Synchronization API for Apps
// ============================================================================

typedef struct {
    os_task_func_t func;
    void *parameter;
} task_wrapper_params_t;

static void task_wrapper(void *pvParameters) {
    task_wrapper_params_t *params = (task_wrapper_params_t *)pvParameters;
    os_task_func_t task_func = params->func;
    void *parameter = params->parameter;

    free(params);

    if (task_func) {
        task_func(parameter);
    }

    vTaskDelete(NULL);
}

os_task_handle_t *os_task_create(os_task_func_t task_func, const char *name, int stack_size, int priority, int core_id) {
    if (!task_func) return NULL;

    os_task_handle_t *handle = malloc(sizeof(os_task_handle_t));
    if (!handle) return NULL;

    task_wrapper_params_t *params = malloc(sizeof(task_wrapper_params_t));
    if (!params) {
        free(handle);
        return NULL;
    }

    params->func = task_func;
    params->parameter = NULL;

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_wrapper,
        name,
        stack_size,
        params,
        priority,
        (TaskHandle_t *)&handle->handle,
        core_id
    );

    if (ret != pdPASS) {
        free(params);
        free(handle);
        return NULL;
    }

    return handle;
}

void os_task_delete(os_task_handle_t *task) {
    if (!task || !task->handle) return;
    vTaskDelete((TaskHandle_t)task->handle);
    free(task);
}

os_semaphore_handle_t *os_semaphore_create(void) {
    os_semaphore_handle_t *handle = malloc(sizeof(os_semaphore_handle_t));
    if (!handle) return NULL;

    handle->handle = xSemaphoreCreateBinary();
    if (!handle->handle) {
        free(handle);
        return NULL;
    }

    return handle;
}

void os_semaphore_give(os_semaphore_handle_t *sem) {
    if (!sem || !sem->handle) return;
    xSemaphoreGive((SemaphoreHandle_t)sem->handle);
}

bool os_semaphore_take(os_semaphore_handle_t *sem, int timeout_ms) {
    if (!sem || !sem->handle) return false;

    TickType_t timeout = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake((SemaphoreHandle_t)sem->handle, timeout) == pdTRUE;
}

void os_semaphore_delete(os_semaphore_handle_t *sem) {
    if (!sem || !sem->handle) return;
    vSemaphoreDelete((SemaphoreHandle_t)sem->handle);
    free(sem);
}
