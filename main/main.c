#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "os_core.h"
#include "boot.h"
#include "hardware.h"

static const char *TAG = "esposito";

void app_main(void)
{
    ESP_LOGI(TAG, "Esposito OS v0.1.0-alpha");
    ESP_LOGI(TAG, "Starting boot sequence...");

    // Force floating-point library functions to be linked for C++ apps
    // This ensures float/double conversion functions are available to apps
    volatile float f = 1.0f;
    volatile double d = 2.0;
    volatile float result = f + (float)d;
    (void)result;

    // Run boot sequence
    boot_sequence();

    // Check if boot was successful
    if (boot_status.stage == BOOT_STAGE_FAILED) {
        ESP_LOGE(TAG, "Boot failed at stage: %s", boot_status.stage_name);
        if (boot_status.error_message) {
            ESP_LOGE(TAG, "Error: %s", boot_status.error_message);
        }
        ESP_LOGE(TAG, "System halted. Please reboot.");
        return;
    }

    ESP_LOGI(TAG, "Starting main event loop...");

    // Main event loop
    os_event_loop();
}
