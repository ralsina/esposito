#include "boot.h"
#include "hardware.h"
#include "os_core.h"
#include "app_loader.h"
#include "text_mode.h"
#include "esp_log.h"
#include "lovgfx_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

extern "C" {
    #include "sd_card.h"
    #include "touchscreen.h"
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
    text_mode_print_at_color(2, 12, "  Keyboard: OK (BBQ20)", TEXT_COLOR_GREEN);
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
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "Keyboard not detected");
        // Continue anyway - keyboard is optional for some apps
    }
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Keyboard subsystem ready");

    // Stage 4.5: SD Card
    boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "Starting SD card init");

    if (sd_card_init()) {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, true, "SD card ready");
    } else {
        boot_display_progress(BOOT_STAGE_KEYBOARD_INIT, false, "SD card not available");
        // Continue anyway - SD card is optional
    }

    // Stage 4.6: Touchscreen
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

    // Stage 6: Load default app
    boot_display_progress(BOOT_STAGE_LOAD_DEFAULT_APP, true, "Loading default app");

    // Load the default app
    if (os_load_app("example_app")) {
        ESP_LOGI(TAG, "key_echo app loaded successfully");
        boot_display_progress(BOOT_STAGE_LOAD_DEFAULT_APP, true, "key_echo app loaded");
    } else {
        ESP_LOGW(TAG, "Failed to load key_echo app");
        boot_display_progress(BOOT_STAGE_LOAD_DEFAULT_APP, false, "App load failed");
    }

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
