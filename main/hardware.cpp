#include "hardware.h"
#include "hardware_config.h"
#include "lovgfx_config.h"
#include "bbq20_keyboard.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <soc/rtc_cntl_reg.h>
#include "spleen-5x8.h"

static const char *TAG = "hardware";

// Display state
LGFX tft;
LGFX* display_tft = &tft;
static bool display_initialized = false;

// I2C state
static bool i2c_initialized = false;
static bool keyboard_initialized = false;

bool hardware_init(void) {
    ESP_LOGI(TAG, "Initializing hardware");

    // Initialize I2C for keyboard (BBQ20 uses I2C address 0x1F)
    i2c_initialized = true;
    ESP_LOGI(TAG, "I2C ready for keyboard (0x1F)");

    if (!display_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return false;
    }

    if (!keyboard_init()) {
        ESP_LOGE(TAG, "Keyboard initialization failed");
        return false;
    }

    if (!timer_init()) {
        ESP_LOGE(TAG, "Timer initialization failed");
        return false;
    }

    if (!serial_init()) {
        ESP_LOGE(TAG, "Serial initialization failed");
        return false;
    }

    ESP_LOGI(TAG, "Hardware initialization complete");
    return true;
}

// Display implementation using LovyanGFX
bool display_init(void) {
    ESP_LOGI(TAG, "Initializing ST7789 display with LovyanGFX");

    tft.begin();
    ESP_LOGI(TAG, "LovyanGFX begin() called");

    tft.setRotation(1);  // Landscape mode
    ESP_LOGI(TAG, "Display rotation set to 1 (landscape)");

    // Don't clear the screen here - let the boot sequence handle it
    // tft.fillScreen(TFT_BLACK);

    ESP_LOGI(TAG, "Display initialization complete");
    display_initialized = true;
    return true;
}

void display_clear(uint16_t color) {
    if (!display_initialized) return;
    tft.fillScreen(color);
}

void display_draw_text(int x, int y, const char *text, uint16_t color) {
    if (!display_initialized) return;
    tft.setCursor(x, y);
    tft.setTextColor(color, TFT_BLACK);  // Set background to black for better contrast
    tft.setFont(&spleen_5x8);  // Use spleen-5x8 font
    tft.print(text);  // Use print instead of println for better control
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if (!display_initialized) return;
    tft.drawPixel(x, y, color);
}

void display_fill_rect(int x, int y, int width, int height, uint16_t color) {
    if (!display_initialized) return;
    tft.fillRect(x, y, width, height, color);
}

// Keyboard implementation for BBQ20 (based on terminado)
bool keyboard_init(void) {
    ESP_LOGI(TAG, "Initializing BBQ20 keyboard driver");

    if (bbq20_keyboard_init()) {
        keyboard_initialized = true;
        ESP_LOGI(TAG, "✅ BBQ20 keyboard driver ready");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ BBQ20 keyboard initialization failed");
        return false;
    }
}

void keyboard_deinit(void) {
    bbq20_keyboard_deinit();
    keyboard_initialized = false;
}

bool keyboard_read_event(event_t *event) {
    static bool first_call = true;

    // Initialize on first call
    if (first_call) {
        ESP_LOGI(TAG, "🎹 BBQ20 keyboard polling started");
        first_call = false;
    }

    // Try to read from BBQ20 keyboard (real or fake fallback)
    bbq20_key_event_t bbq20_event;

    if (bbq20_read_key_event(&bbq20_event)) {
        event->type = EVENT_KEYBOARD;
        event->keyboard.key = (char)bbq20_event.key_code;
        event->keyboard.pressed = bbq20_event.pressed;
        event->keyboard.modifiers = bbq20_event.modifiers;  // Include modifier state

        return true;
    }

    return false;
}

// Timer stubs
bool timer_init(void) {
    ESP_LOGI(TAG, "Timer init");
    return true;
}

void timer_set_interval(uint32_t interval_ms) {
    // TODO: Implement
}

// Serial stubs
bool serial_init(void) {
    ESP_LOGI(TAG, "Serial init");
    return true;
}

size_t serial_read(char *buffer, size_t max_len) {
    // TODO: Implement
    return 0;
}
