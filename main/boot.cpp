#include "boot.h"
#include "hardware.h"
#include "os_core.h"
#include "app_heap.h"
#include "app_loader.h"

#include "app_config.h"
#include "text_mode.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lovgfx_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <time.h>

extern "C" {
    #include "sd_card.h"
    #include "touchscreen.h"
    #include "wifi.h"
    #include "ota_update.h"
}

extern "C" bool font_cache_init(void);

static const char *TAG = "boot";

boot_status_t boot_status = {
    .stage = BOOT_STAGE_POWER_ON,
    .stage_name = "Power On",
    .success = true,
    .error_message = NULL
};

static const char* boot_stage_names[] = {
    "Power On",
    "Hardware Init",
    "Display Init",
    "Filesystem Init",
    "Keyboard Init",
    "App Loader Init",
    "Load Default App",
    "Boot Complete",
    "Boot Failed"
};

static void boot_report_app_memory(void) {
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest_8bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG, "App memory budget:");
    ESP_LOGI(TAG, "  Heap free (8-bit): %u bytes (%.1f KiB)",
             (unsigned)free_8bit, (double)free_8bit / 1024.0);
    ESP_LOGI(TAG, "  Largest block (8-bit): %u bytes (%.1f KiB)",
             (unsigned)largest_8bit, (double)largest_8bit / 1024.0);
    ESP_LOGI(TAG, "  Internal heap free: %u bytes (%.1f KiB)",
             (unsigned)free_internal, (double)free_internal / 1024.0);
    ESP_LOGI(TAG, "  Largest internal block: %u bytes (%.1f KiB)",
             (unsigned)largest_internal, (double)largest_internal / 1024.0);
    app_heap_log_stats("App heap");
}

static void boot_apply_log_output_setting(void) {
    bool enabled = serial_log_output_is_enabled();
    if (config_bind_app("settings")) {
        enabled = appcfg_get_bool("serial_log_output", false);
        config_unbind_app();
    } else {
        ESP_LOGW(TAG, "Settings config unavailable; keeping serial log output enabled");
    }
    serial_log_output_set_enabled(enabled);
}

static int boot_display_row = 0;

void boot_display_progress(boot_stage_t stage, bool success, const char *message) {
    boot_status.stage = stage;
    boot_status.stage_name = boot_stage_names[stage];
    boot_status.success = success;
    boot_status.error_message = message;

    if (success) {
        ESP_LOGI(TAG, "  %s", boot_status.stage_name);
    } else {
        ESP_LOGE(TAG, "  ERROR: %s", boot_status.stage_name);
    }

    if (stage >= BOOT_STAGE_DISPLAY_INIT) {
        if (boot_display_row > 0 && boot_display_row < TEXT_MODE_ROWS - 1) {
            uint16_t color = success ? TEXT_COLOR_GREEN : TEXT_COLOR_RED;
            const char *marker = success ? " OK" : " FAIL";
            text_mode_print_at_color(TEXT_MODE_COLS - 5, boot_display_row, marker, color);
        }
        if (message && boot_display_row < TEXT_MODE_ROWS - 2) {
            boot_display_row++;
            text_mode_print_at_color(2, boot_display_row, message, TEXT_COLOR_WHITE);
        }
        text_mode_flush();
    }
}

bool boot_display_init(void) {
    ESP_LOGI(TAG, "=== Esposito OS Boot ===");
    ESP_LOGI(TAG, "Version: 0.1.0-alpha");
    ESP_LOGI(TAG, "Hardware: %s", BOARD_NAME);

    // Try to initialize display
    if (!display_init()) {
        ESP_LOGW(TAG, "Display initialization failed, continuing with serial only");
        boot_display_progress(BOOT_STAGE_DISPLAY_INIT, false, "Display not available");
        return false;
    }

    // Display splash screen
    boot_display_splash();

    boot_display_progress(BOOT_STAGE_DISPLAY_INIT, true, "Display ready");
    return true;
}

void boot_display_splash(void) {
    ESP_LOGI(TAG, "Displaying splash screen...");

    // Initialize text mode
    text_mode_init();

    // Clear screen with black background
    text_mode_clear(TEXT_COLOR_BLACK);

    // Draw a border
    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, 0, "*", TEXT_COLOR_CYAN);
        text_mode_print_at_color(x, TEXT_MODE_ROWS - 1, "*", TEXT_COLOR_CYAN);
    }
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        text_mode_print_at_color(0, y, "*", TEXT_COLOR_CYAN);
        text_mode_print_at_color(TEXT_MODE_COLS - 1, y, "*", TEXT_COLOR_CYAN);
    }

    // Draw title in green at top
    text_mode_print_at_color((TEXT_MODE_COLS - 12) / 2, 2, "Esposito OS", TEXT_COLOR_GREEN);

    // Draw version in cyan below title
    text_mode_print_at_color((TEXT_MODE_COLS - 11) / 2, 3, "v0.1.0-alpha", TEXT_COLOR_CYAN);

    // Set starting row for progress messages
    boot_display_row = 5;
    text_mode_print_at_color(2, boot_display_row, "Booting...", TEXT_COLOR_YELLOW);

    ESP_LOGI(TAG, "Splash screen displayed");
}

static bool boot_check_crash_loop(void) {
    time_t now = time(NULL);

    if (now < 1700000000LL) {
        ESP_LOGI(TAG, "RTC not set (now=%lld), skipping crash-loop detection", (long long)now);
        return false;
    }

    if (!config_bind_app("settings")) {
        ESP_LOGE(TAG, "Cannot bind settings for crash-loop check");
        return false;
    }

    int boot_count = appcfg_get_int("system/boot_count", 0);
    time_t last_boot = (time_t)appcfg_get_int("system/last_boot_time", 0);
    double elapsed = difftime(now, last_boot);

    ESP_LOGI(TAG, "Boot check: count=%d, elapsed=%.0fs", boot_count, elapsed);

    bool crash_loop = false;

    if (last_boot > 0 && elapsed >= 0 && elapsed < 15.0) {
        boot_count++;
        ESP_LOGW(TAG, "Rapid reboot #%d (%.0fs since last boot)", boot_count, elapsed);
        if (boot_count >= 3) {
            ESP_LOGE(TAG, "Crash loop detected! Clearing last app.");
            config_delete("system/last_app");
            appcfg_set_int("system/boot_count", 0);
            appcfg_set_int("system/last_boot_time", 0);
            crash_loop = true;
        }
    } else {
        boot_count = 1;
    }

    if (!crash_loop) {
        appcfg_set_int("system/boot_count", boot_count);
        appcfg_set_int("system/last_boot_time", (int)now);
    }

    config_unbind_app();
    return crash_loop;
}

static void boot_auto_load_last_app(void) {
    if (!config_bind_app("settings")) {
        return;
    }

    char last_app[64] = {0};
    size_t len = appcfg_get_string("system/last_app", "", last_app, sizeof(last_app));
    config_unbind_app();

    if (len == 0 || last_app[0] == '\0') {
        ESP_LOGI(TAG, "No saved last app, starting launcher");
        return;
    }

    ESP_LOGI(TAG, "Auto-loading last app: %s", last_app);
    if (!os_load_app(last_app)) {
        ESP_LOGW(TAG, "Failed to load '%s', starting launcher", last_app);
        if (config_bind_app("settings")) {
            config_delete("system/last_app");
            config_unbind_app();
        }
    }
}

void boot_sequence(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     Esposito OS Boot Sequence        ║");
    ESP_LOGI(TAG, "║     %-31s║", BOARD_NAME);
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "Reserving app heap early");
    if (!app_heap_init()) {
        ESP_LOGE(TAG, "App heap reservation failed");
        boot_status.stage = BOOT_STAGE_FAILED;
        boot_status.stage_name = "Boot Failed";
        boot_status.success = false;
        boot_status.error_message = "App heap reservation failed";
        return;
    }
    boot_report_app_memory();

    // Stage 1: Display initialization
    boot_display_init();

    // Stage 2: Hardware initialization
    boot_display_progress(BOOT_STAGE_HARDWARE_INIT, true, "Starting hardware init");

    if (!hardware_init()) {
        boot_display_progress(BOOT_STAGE_HARDWARE_INIT, false, "Hardware initialization failed");
        boot_status.stage = BOOT_STAGE_FAILED;
        return;
    }
    boot_display_progress(BOOT_STAGE_HARDWARE_INIT, true, "Hardware ready");

    // Stage 3: Filesystem
    boot_display_progress(BOOT_STAGE_FILESYSTEM_INIT, true, "Starting filesystem init");

    if (!os_init_filesystem()) {
        boot_display_progress(BOOT_STAGE_FILESYSTEM_INIT, false, "Filesystem initialization failed");
        boot_status.stage = BOOT_STAGE_FAILED;
        return;
    }
    boot_display_progress(BOOT_STAGE_FILESYSTEM_INIT, true, "Filesystem ready");

    // Stage 4: Keyboard
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Starting keyboard init");

    if (!keyboard_init()) {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "Keyboard not detected (optional)");
    } else {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Keyboard ready");
    }

    // Stage 4.5: SD Card (required - all apps live on SD)
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Starting SD card init");

    if (!sd_card_init()) {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "SD card required!");
        text_mode_print_at_color(2, boot_display_row + 2, "Insert SD card and reboot.", TEXT_COLOR_YELLOW);
        text_mode_print_at_color(2, boot_display_row + 3, "No apps available without SD.", TEXT_COLOR_YELLOW);
        text_mode_flush();
        ESP_LOGE(TAG, "SD card not available - halting boot");
        while (1) { vTaskDelay(1000 / portTICK_PERIOD_MS); }
    }
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "SD card ready");

    ota_recovery_check();

    ESP_LOGI(TAG, "==== Applying configured display settings ====");
    display_apply_saved_rotation();
    display_apply_saved_backlight();

    // Stage 4.6: WiFi
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Starting WiFi init");

    if (wifi_init()) {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "WiFi ready");
    } else {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "WiFi not available");
        // Continue anyway - WiFi is optional
    }

    // Stage 4.7: Touchscreen
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Starting touchscreen init");

    if (touchscreen_init()) {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Touchscreen ready");
    } else {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "Touchscreen not available");
        // Continue anyway - touchscreen is optional
    }

    // Stage 5: App loader
    boot_display_progress(BOOT_STAGE_APP_LOADER_INIT, true, "Starting app loader");

    if (!app_loader_init()) {
        boot_display_progress(BOOT_STAGE_APP_LOADER_INIT, false, "App loader initialization failed");
        boot_status.stage = BOOT_STAGE_FAILED;
        return;
    }
    boot_display_progress(BOOT_STAGE_APP_LOADER_INIT, true, "App loader ready");
    boot_report_app_memory();

    font_cache_init();
    text_mode_apply_configured_font();

    int palette_index = os_settings_get_int("display/palette", 0);
    text_mode_apply_configured_palette(palette_index);

    bool crash_loop = boot_check_crash_loop();
    if (crash_loop) {
        ESP_LOGW(TAG, "Crash loop detected, starting launcher instead of last app");
    } else {
        char last_app[64] = {0};
        size_t len = 0;
        if (config_bind_app("settings")) {
            len = appcfg_get_string("system/last_app", "", last_app, sizeof(last_app));
            config_unbind_app();
        }
        if (len > 0 && last_app[0] != '\0') {
            char msg[80];
            snprintf(msg, sizeof(msg), "Starting %s...", last_app);
            boot_display_progress(BOOT_STAGE_LOAD_DEFAULT_APP, true, msg);
        }
        boot_auto_load_last_app();
    }

    if (os_get_current_app() == NULL) {
        os_load_app("launcher");
    }

    // For now, just say we're ready
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     BOOT SEQUENCE COMPLETE            ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    boot_status.stage = BOOT_STAGE_COMPLETE;
    // Only update display if no app is running (otherwise app's own rendering is already on screen)
    if (os_get_current_app() == NULL) {
        boot_display_progress(BOOT_STAGE_COMPLETE, true, "System ready");
    } else {
        ESP_LOGI(TAG, "  %s", boot_status.stage_name);
    }

    // Show available apps (if any)
    int app_count = app_loader_get_count();
    ESP_LOGI(TAG, "Available apps: %d", app_count);
}
