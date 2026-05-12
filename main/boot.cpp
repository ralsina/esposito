#include "boot.h"
#include "hardware.h"
#include "os_core.h"
#include "app_loader.h"
#include "esp_log.h"
#include "lovgfx_config.h"
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

    // Clear screen with a dark blue background
    display_clear(0x001F);  // Dark blue

    // Add a delay to make sure display is ready
    vTaskDelay(pdMS_TO_TICKS(200));

    // Draw title in large green text at top
    display_draw_text(5, 5, "Esposito OS", TFT_GREEN);

    // Draw version in white below title
    display_draw_text(5, 30, "v0.1.0-alpha", TFT_WHITE);

    // Draw status in yellow
    display_draw_text(5, 55, "System booting...", TFT_YELLOW);

    // Draw some visual indicators
    for (int i = 0; i < 50; i++) {
        display_draw_pixel(5 + i * 2, 80, TFT_GREEN);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

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

    // Load the key_echo app
    if (os_load_app("key_echo")) {
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
