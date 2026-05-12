// Touchscreen Probe App
// Tests different SPI configurations for XPT2046 touchscreen

#include "os_core.h"
#include "hardware.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <stdio.h>

static const char *TAG = "touch_probe";

// Test configurations to try
typedef struct {
    const char *name;
    int mosi;
    int miso;
    int clk;
    int cs;
    int irq;
} spi_config_t;

static app_context_t *app_ctx = NULL;

static void app_init(app_context_t *ctx) {
    app_ctx = ctx;
    ESP_LOGI(TAG, "Touchscreen probe app initialized");
}

static bool test_spi_touchscreen(const spi_config_t *cfg) {
    ESP_LOGI(TAG, "Testing SPI: %s", cfg->name);

    // Check IRQ first (don't use pullup for GPIO 36 - it's input-only)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << cfg->irq),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,  // GPIO 36 is input-only
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    int irq_level = gpio_get_level(cfg->irq);
    ESP_LOGI(TAG, "  IRQ pin %d: level=%d", cfg->irq, irq_level);

    // Try to add SPI device to existing SPI3 bus (used by SD card)
    spi_device_handle_t spi_handle = NULL;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2500000,
        .mode = 0,
        .spics_io_num = cfg->cs,
        .queue_size = 1,
    };

    // Try to add device - this will work if pins match the bus config
    esp_err_t ret = spi_bus_add_device(SPI3_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "  SPI device add failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Try XPT2046 command
    spi_transaction_t trans = {
        .length = 8,
        .rxlength = 8,
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .tx_data[0] = 0x90,  // XPT2046 X position command
    };

    bool success = false;
    if (spi_device_transmit(spi_handle, &trans) == ESP_OK) {
        uint16_t value = (trans.rx_data[0] << 8 | trans.rx_data[1]) >> 3;
        ESP_LOGI(TAG, "  SPI response: 0x%04X", value);
        if (value != 0 && value != 0xFFFF) {
            success = true;
        }
    }

    spi_bus_remove_device(spi_handle);

    return success;
}

static void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        display_clear(0x001F); // Blue background
        display_draw_text(5, 5, "Touch Probe", 0xFFFF);

        int y = 30;
        char msg[64];

        // Test different SPI pin configurations
        spi_config_t tests[] = {
            {"SD Card SPI", 23, 19, 18, 33, 36},  // Share SD card's SPI3 pins
            {"Standard CYD", 32, 39, 25, 33, 36},   // From witnessmenow
            {"Display SPI", 13, 12, 14, 33, 36},    // Share display's SPI2 pins
            {"Alt config", 32, 19, 18, 33, 36},     // Mix of both
        };

        for (int i = 0; i < 4; i++) {
            if (test_spi_touchscreen(&tests[i])) {
                snprintf(msg, sizeof(msg), "✓ %s: WORKING!", tests[i].name);
                display_draw_text(5, y, msg, 0x07E0); // Green
                ESP_LOGI(TAG, "SUCCESS: %s touchscreen detected!", tests[i].name);
            } else {
                snprintf(msg, sizeof(msg), "✗ %s: failed", tests[i].name);
                display_draw_text(5, y, msg, 0xF800); // Red
            }
            y += 20;
        }

        ESP_LOGI(TAG, "Probe complete. Press any key to probe again.");
    }
}

static void app_close(app_context_t *ctx) {
    ESP_LOGI(TAG, "Touch probe app closing");
}

static void app_checkpoint(app_context_t *ctx) {
}

app_manifest_t app_manifest = {
    .name = "touch_probe",
    .init = app_init,
    .event_fn = app_event,
    .close = app_close,
    .checkpoint = app_checkpoint,
    .subscriptions = EVENT_KEYBOARD
};