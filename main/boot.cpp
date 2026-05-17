#include "boot.h"
#include "hardware.h"
#include "os_core.h"
#include "app_heap.h"
#include "app_loader.h"
#include "app_launcher.h"
#include "app_config.h"
#include "text_mode.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lovgfx_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

extern "C" {
    #include "sd_card.h"
    #include "touchscreen.h"
    #include "wifi.h"
}

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
        enabled = config_get_bool("serial_log_output", false);
        config_unbind_app();
    } else {
        ESP_LOGW(TAG, "Settings config unavailable; keeping serial log output enabled");
    }
    serial_log_output_set_enabled(enabled);
}

void boot_display_progress(boot_stage_t stage, bool success, const char *message) {
    // Update boot status
    boot_status.stage = stage;
    boot_status.stage_name = boot_stage_names[stage];
    boot_status.success = success;
    boot_status.error_message = message;

    // Log to serial
    if (success) {
        ESP_LOGI(TAG, "✓ %s", boot_status.stage_name);
        if (message) {
            ESP_LOGI(TAG, "  %s", message);
        }
    } else {
        ESP_LOGE(TAG, "✗ %s", boot_status.stage_name);
        if (message) {
            ESP_LOGE(TAG, "  ERROR: %s", message);
        }
    }

    // Display on screen (if display is working)
    if (stage >= BOOT_STAGE_DISPLAY_INIT) {
        // TODO: Update boot screen on display
        // For now, just show progress via serial
    }
}

bool boot_display_init(void) {
    ESP_LOGI(TAG, "=== Esposito OS Boot ===");
    ESP_LOGI(TAG, "Version: 0.1.0-alpha");
    ESP_LOGI(TAG, "Hardware: ESP32 CYD (2USB version)");

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

    // Add a delay to make sure display is ready
    vTaskDelay(pdMS_TO_TICKS(200));

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

    // Draw status in yellow
    text_mode_print_at_color((TEXT_MODE_COLS - 16) / 2, 5, "System booting...", TEXT_COLOR_YELLOW);

    // Draw some visual indicators
    for (int i = 0; i < 20; i++) {
        text_mode_printf_at_color(10 + i, 7, TEXT_COLOR_GREEN, "*");
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Show hardware status
    text_mode_print_at_color(2, 10, "Hardware:", TEXT_COLOR_WHITE);
    text_mode_print_at_color(2, 11, "  Display: OK (ST7789)", TEXT_COLOR_GREEN);
    text_mode_print_at_color(2, 12, "  Keyboard: Optional (BBQ20)", TEXT_COLOR_YELLOW);
    text_mode_print_at_color(2, 13, "  SD Card: OK (FAT32)", TEXT_COLOR_GREEN);
    text_mode_print_at_color(2, 14, "  Touch: OK (XPT2046)", TEXT_COLOR_GREEN);

    // Instructions
    text_mode_print_at_color(2, 17, "Press Ctrl+ESC for app launcher", TEXT_COLOR_YELLOW);

    ESP_LOGI(TAG, "Splash screen displayed");
}

void boot_sequence(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     Esposito OS Boot Sequence        ║");
    ESP_LOGI(TAG, "║     ESP32 CYD (2USB version)         ║");
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
    boot_apply_log_output_setting();

    // Stage 4: Keyboard
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Starting keyboard init");

    if (!keyboard_init()) {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "Keyboard not detected (optional)");
    } else {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Keyboard ready");
    }

    // Stage 4.5: SD Card
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Starting SD card init");

    if (sd_card_init()) {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "SD card ready");

        // Now that SD card is mounted, apply user's configured font
        ESP_LOGI(TAG, "==== Applying configured font settings ====");
        bool font_applied = text_mode_apply_configured_font();
        ESP_LOGI(TAG, "Font apply result: %s", font_applied ? "SUCCESS" : "FAILED");
    } else {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "SD card not available");
        // Continue anyway - SD card is optional
    }

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

    // Stage 6: Show launcher
    boot_display_progress(BOOT_STAGE_LOAD_DEFAULT_APP, true, "Starting app launcher");

    app_launcher_start();

    // For now, just say we're ready
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     BOOT SEQUENCE COMPLETE            ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    boot_status.stage = BOOT_STAGE_COMPLETE;
    boot_display_progress(BOOT_STAGE_COMPLETE, true, "System ready");

    // Show available apps (if any)
    int app_count = app_loader_get_count();
    ESP_LOGI(TAG, "Available apps: %d", app_count);
}
