#include "hardware.h"
#include "hardware_config.h"
#include "lovgfx_config.h"
#include "bbq20_keyboard.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <soc/rtc_cntl_reg.h>
#include "fonts.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <string.h>

extern "C" {
#include "sd_card.h"
}

static const char *TAG = "hardware";

// Display state
LGFX tft;
LGFX* display_tft = &tft;
static bool display_initialized = false;
static const lgfx::IFont *current_display_font = NULL;
static int disp_font_width = 5;
static int disp_font_height = 8;

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
        ESP_LOGW(TAG, "Keyboard initialization failed, continuing without keyboard");
    }

    // Configure BOOT button (GPIO 0) as input with pull-up
    gpio_config_t boot_btn = {
        .pin_bit_mask = (1ULL << GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&boot_btn);
    ESP_LOGI(TAG, "BOOT button (GPIO 0) configured");

    if (!timer_init()) {
        ESP_LOGE(TAG, "Timer initialization failed");
        return false;
    }

    // Serial is not initialized at boot — each app calls serial_init() with its own config

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

void display_set_font(const void *font) {
    current_display_font = (const lgfx::IFont *)font;
    for (int i = 0; i < FONT_COUNT; i++) {
        if (font_table[i].font_ptr == font) {
            disp_font_width = font_table[i].char_width;
            disp_font_height = font_table[i].char_height;
            return;
        }
    }
}

void display_clear(uint16_t color) {
    if (!display_initialized) return;
    tft.fillScreen(color);
}

void display_draw_text(int x, int y, const char *text, uint16_t color) {
    if (!display_initialized) return;
    tft.setCursor(x, y);
    tft.setTextColor(color, TFT_BLACK);
    if (current_display_font) tft.setFont(current_display_font);
    tft.print(text);
}

void display_draw_text_transparent(int x, int y, const char *text, uint16_t color) {
    if (!display_initialized) return;
    tft.setCursor(x, y);
    tft.setTextColor(color);
    if (current_display_font) tft.setFont(current_display_font);
    tft.print(text);
}

void display_draw_text_bg(int x, int y, const char *text, uint16_t fg, uint16_t bg) {
    if (!display_initialized) return;
    tft.setCursor(x, y);
    tft.setTextColor(fg, bg);
    if (current_display_font) tft.setFont(current_display_font);
    tft.print(text);
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if (!display_initialized) return;
    tft.drawPixel(x, y, color);
}

void display_fill_rect(int x, int y, int width, int height, uint16_t color) {
    if (!display_initialized) return;
    tft.fillRect(x, y, width, height, color);
}

// Font cell size used by text mode (spleen-5x8)
#define DISPLAY_FONT_WIDTH  5
#define DISPLAY_FONT_HEIGHT 8

void display_draw_char_at(int x, int y, char ch, uint16_t fg_color, uint16_t bg_color) {
    if (!display_initialized) return;
    tft.fillRect(x, y, disp_font_width, disp_font_height, bg_color);
    if (ch != ' ') {
        tft.setCursor(x, y);
        tft.setTextColor(fg_color, bg_color);
        if (current_display_font) tft.setFont(current_display_font);
        tft.print(ch);
    }
}

bool display_save_screenshot_ppm(const char *path) {
    if (!path || !display_initialized || !display_tft) return false;
    if (!sd_card_is_mounted()) return false;

    FILE *fppm = fopen(path, "wb");
    if (!fppm) return false;

    const int width = 320;
    const int height = 240;
    fprintf(fppm, "P6\n%d %d\n255\n", width, height);

    uint8_t row_buf[960];
    for (int y = 0; y < height; y++) {
        uint8_t *p = row_buf;
        for (int x = 0; x < width; x++) {
            uint16_t rgb565 = display_tft->readPixel(x, y);
            uint8_t r = (rgb565 >> 8) & 0xF8; r |= r >> 5;
            uint8_t g = (rgb565 >> 3) & 0xFC; g |= g >> 6;
            uint8_t b = (rgb565 << 3) & 0xF8; b |= b >> 5;
            *p++ = r;
            *p++ = g;
            *p++ = b;
        }
        fwrite(row_buf, 1, sizeof(row_buf), fppm);
    }

    fclose(fppm);
    return true;
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

bool keyboard_is_available(void) {
    return keyboard_initialized;
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
        event->keyboard.raw_key_code = bbq20_event.raw_key_code;

        ESP_LOGI(TAG,
                 "KB EVT enqueue: key=%d(0x%02x) raw=0x%02x mod=0x%02x pressed=%d",
                 event->keyboard.key,
                 (uint8_t)event->keyboard.key,
                 event->keyboard.raw_key_code,
                 event->keyboard.modifiers,
                 event->keyboard.pressed);

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

// Serial
static bool serial_initialized = false;
static vprintf_like_t default_vprintf = NULL;
static bool serial_log_output_enabled = true;

static int noop_vprintf(const char *fmt, va_list args) {
    (void)fmt;
    (void)args;
    return 0;
}

static uart_word_length_t serial_data_bits_map(int data_bits) {
    switch (data_bits) {
        case 5: return UART_DATA_5_BITS;
        case 6: return UART_DATA_6_BITS;
        case 7: return UART_DATA_7_BITS;
        default: return UART_DATA_8_BITS;
    }
}

static uart_parity_t serial_parity_map(char parity) {
    switch (parity) {
        case 'O': case 'o': return UART_PARITY_ODD;
        case 'E': case 'e': return UART_PARITY_EVEN;
        default: return UART_PARITY_DISABLE;
    }
}

static uart_stop_bits_t serial_stop_bits_map(int stop_bits) {
    switch (stop_bits) {
        case 2: return UART_STOP_BITS_2;
        default: return UART_STOP_BITS_1;
    }
}

bool serial_init(int baud, int data_bits, char parity, int stop_bits) {
    esp_err_t ret = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (ret == ESP_ERR_INVALID_STATE) {
        // Driver already installed, still fine
    } else if (ret != ESP_OK) {
        return false;
    }

    uart_config_t uart_config;
    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.baud_rate = baud;
    uart_config.data_bits = serial_data_bits_map(data_bits);
    uart_config.parity = serial_parity_map(parity);
    uart_config.stop_bits = serial_stop_bits_map(stop_bits);
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;
    ret = uart_param_config(UART_NUM_0, &uart_config);
    if (ret != ESP_OK) {
        return false;
    }
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Drain any stale data from the RX FIFO
    uint8_t dummy;
    while (uart_read_bytes(UART_NUM_0, &dummy, 1, 0) > 0) {}

    serial_initialized = true;
    return true;
}

void serial_deinit(void) {
    if (serial_initialized) {
        serial_initialized = false;
        uart_driver_delete(UART_NUM_0);
    }
}

size_t serial_read(char *buffer, size_t max_len) {
    if (!serial_initialized || !buffer || max_len == 0) return 0;
    size_t len = 0;
    esp_err_t ret = uart_get_buffered_data_len(UART_NUM_0, &len);
    if (ret != ESP_OK || len == 0) return 0;
    if (len > max_len) len = max_len;
    int read = uart_read_bytes(UART_NUM_0, buffer, len, 0);
    return read > 0 ? (size_t)read : 0;
}

size_t serial_write(const char *data, size_t len) {
    if (!serial_initialized || !data || len == 0) return 0;
    int written = uart_write_bytes(UART_NUM_0, data, len);
    return written > 0 ? (size_t)written : 0;
}

void serial_log_output_set_enabled(bool enabled) {
    if (enabled == serial_log_output_enabled) {
        return;
    }

    if (!enabled) {
        default_vprintf = esp_log_set_vprintf(noop_vprintf);
        serial_log_output_enabled = false;
    } else {
        if (default_vprintf) {
            esp_log_set_vprintf(default_vprintf);
        }
        serial_log_output_enabled = true;
    }
}

bool serial_log_output_is_enabled(void) {
    return serial_log_output_enabled;
}
