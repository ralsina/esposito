#include "hardware.h"
#include "hardware_config.h"
#include "lovgfx_config.h"
#if BOARD_HAS_BBQ20_KEYBOARD
#include "bbq20_keyboard.h"
#endif
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fonts.h"
#include <lgfx/v1/lgfx_fonts.hpp>
#include <lgfx/utility/pgmspace.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#if defined(BOARD_LED_RED_PIN)
#include "driver/ledc.h"
#endif
#include "driver/i2c_types.h"
#if BOARD_BACKLIGHT_I2C_EXPANDER
#include "driver/i2c_master.h"
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "sd_card.h"
#include "os_core.h"
#include "text_mode.h"
}

static const char *TAG = "hardware";

// Minimal POSIX FILE DataWrapper for direct JPEG decoding without LovyanGFX's
// file-factory machinery (which sets need_transaction=true and may disrupt the
// display SPI transaction even when SD and display are on separate buses).
struct PosixFileWrapper : public lgfx::DataWrapper {
    PosixFileWrapper(FILE *fp) : lgfx::DataWrapper(), _fp(fp) {
        need_transaction = false;
    }
    int read(uint8_t *buf, uint32_t len) override { return fread(buf, 1, len, _fp); }
    void skip(int32_t offset) override { fseek(_fp, offset, SEEK_CUR); }
    bool seek(uint32_t offset) override { return fseek(_fp, offset, SEEK_SET) == 0; }
    void close(void) override { }
    int32_t tell(void) override { return ftell(_fp); }
private:
    FILE *_fp;
};

// Display state
LGFX tft;
LGFX* display_tft = &tft;
static bool display_initialized = false;
static int disp_font_width = 5;
static int disp_font_height = 8;
static int current_rotation = 1;  // Default to landscape mode

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
        .pin_bit_mask = (1ULL << BOARD_BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&boot_btn);
    ESP_LOGI(TAG, "BOOT button configured");

    if (!timer_init()) {
        ESP_LOGE(TAG, "Timer initialization failed");
        return false;
    }

    // Serial is not initialized at boot — each app calls serial_init() with its own config

    ESP_LOGI(TAG, "Hardware initialization complete");
    return true;
}

#if BOARD_BACKLIGHT_I2C_EXPANDER
static bool backlight_i2c_init(void) {
    ESP_LOGI(TAG, "Initializing backlight via I2C IO expander (addr=0x%02X, pin=%d)",
             BOARD_BACKLIGHT_I2C_ADDR, BOARD_BACKLIGHT_I2C_PIN);

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = BOARD_I2C_PORT;
    bus_cfg.sda_io_num = (gpio_num_t)BOARD_I2C_SDA;
    bus_cfg.scl_io_num = (gpio_num_t)BOARD_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus creation failed: %s", esp_err_to_name(ret));
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = BOARD_BACKLIGHT_I2C_ADDR;
    dev_cfg.scl_speed_hz = BOARD_I2C_FREQ;

    i2c_master_dev_handle_t dev_handle;
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        i2c_del_master_bus(bus_handle);
        return false;
    }

    uint8_t val = (1 << BOARD_BACKLIGHT_I2C_PIN);
    ret = i2c_master_transmit(dev_handle, &val, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write to IO expander failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev_handle);
        i2c_del_master_bus(bus_handle);
        return false;
    }

    ESP_LOGI(TAG, "Backlight enabled via IO expander pin %d", BOARD_BACKLIGHT_I2C_PIN);

    i2c_master_bus_rm_device(dev_handle);
    i2c_del_master_bus(bus_handle);
    return true;
}
#endif

// Display implementation using LovyanGFX
bool display_init(void) {
    // Idempotent: boot.cpp and hardware_init() both call this; only the first
    // call must run LovyanGFX begin() (a second begin() reconfigures the panel
    // and can leave the display in a partial state).
    if (display_initialized) return true;

    ESP_LOGI(TAG, "Initializing display with LovyanGFX");

#if BOARD_BACKLIGHT_I2C_EXPANDER
    if (!backlight_i2c_init()) {
        ESP_LOGW(TAG, "I2C backlight init failed, continuing anyway");
    }
#endif

    tft.begin();
    ESP_LOGI(TAG, "LovyanGFX begin() called");

    // Set default rotation (will be overridden by settings later when SD card is available)
    current_rotation = DEFAULT_DISPLAY_ROTATION;
    tft.setRotation(current_rotation);
    ESP_LOGI(TAG, "Display rotation set to default %d", current_rotation);

    // Don't clear the screen here - let the boot sequence handle it
    // tft.fillScreen(TFT_BLACK);

    ESP_LOGI(TAG, "Display initialization complete");
    display_initialized = true;
    return true;
}

bool display_load_font(font_id_t id, font_variant_t variant) {
    if (id < 0 || id >= font_count) {
        ESP_LOGE(TAG, "Invalid font ID: %d", id);
        return false;
    }

    size_t data_size;
    const uint8_t *data = font_get_variant_data(id, variant, &data_size);
    if (!data) {
        return false;
    }

    disp_font_width = font_table[id].char_width;
    disp_font_height = font_table[id].char_height;

    bool ok = tft.loadFont(data);
    if (ok) {
        ESP_LOGD(TAG, "Loaded font %s variant %d (%dx%d)",
                 font_table[id].name, variant, disp_font_width, disp_font_height);
    } else {
        disp_font_width = 5;
        disp_font_height = 8;
        ESP_LOGE(TAG, "Failed to load font %s variant %d", font_table[id].name, variant);
    }
    return ok;
}

void display_clear(uint16_t color) {
    if (!display_initialized) return;
    tft.fillScreen(color);
}

void display_draw_text(int x, int y, const char *text, uint16_t color) {
    if (!display_initialized) return;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(text, x, y);
}

void display_draw_text_transparent(int x, int y, const char *text, uint16_t color) {
    if (!display_initialized) return;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color);
    tft.drawString(text, x, y);
}

void display_draw_text_bg(int x, int y, const char *text, uint16_t fg, uint16_t bg) {
    if (!display_initialized) return;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(fg, bg);
    tft.drawString(text, x, y);
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if (!display_initialized) return;
    tft.drawPixel(x, y, color);
}

void display_fill_rect(int x, int y, int width, int height, uint16_t color) {
    if (!display_initialized) return;
    tft.fillRect(x, y, width, height, color);
}

static bool jpeg_is_sof_marker(int marker) {
    switch (marker) {
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCD:
        case 0xCE:
        case 0xCF:
            return true;
        default:
            return false;
    }
}

static bool jpeg_read_be16(FILE *file, int *value) {
    int high = fgetc(file);
    int low = fgetc(file);

    if (high == EOF || low == EOF) {
        return false;
    }

    *value = (high << 8) | low;
    return true;
}

bool display_get_jpg_size(const char *path, int *width, int *height) {
    if (width != NULL) {
        *width = 0;
    }
    if (height != NULL) {
        *height = 0;
    }

    if (path == NULL) {
        ESP_LOGE(TAG, "display_get_jpg_size: path is NULL");
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "display_get_jpg_size: cannot open %s", path);
        return false;
    }

    int soi_high = fgetc(file);
    int soi_low = fgetc(file);
    if (soi_high != 0xFF || soi_low != 0xD8) {
        ESP_LOGE(TAG, "display_get_jpg_size: invalid JPEG header (0x%02X 0x%02X)", soi_high, soi_low);
        fclose(file);
        return false;
    }

    ESP_LOGI(TAG, "JPEG header OK, scanning for SOF marker...");

    int markers_checked = 0;
    while (true) {
        int prefix = fgetc(file);
        if (prefix == EOF) {
            ESP_LOGE(TAG, "display_get_jpg_size: reached EOF without finding SOF (checked %d markers)", markers_checked);
            break;
        }
        if (prefix != 0xFF) {
            continue;
        }

        int marker = fgetc(file);
        while (marker == 0xFF) {
            marker = fgetc(file);
        }

        if (marker == EOF || marker == 0xD9 || marker == 0xDA) {
            ESP_LOGE(TAG, "display_get_jpg_size: hit end marker 0x%02X without SOF", marker);
            break;
        }

        if (marker >= 0xD0 && marker <= 0xD7) {
            continue;
        }

        int segment_length = 0;
        if (!jpeg_read_be16(file, &segment_length) || segment_length < 2) {
            ESP_LOGE(TAG, "display_get_jpg_size: invalid segment length at marker 0x%02X", marker);
            break;
        }

        markers_checked++;

        if (jpeg_is_sof_marker(marker)) {
            ESP_LOGI(TAG, "Found SOF marker 0x%02X at marker #%d", marker, markers_checked);
            if (fgetc(file) == EOF) {
                ESP_LOGE(TAG, "display_get_jpg_size: EOF reading precision byte");
                break;
            }

            int jpeg_height = 0;
            int jpeg_width = 0;
            if (!jpeg_read_be16(file, &jpeg_height) || !jpeg_read_be16(file, &jpeg_width)) {
                ESP_LOGE(TAG, "display_get_jpg_size: failed to read dimensions");
                break;
            }

            fclose(file);

            if (width != NULL) {
                *width = jpeg_width;
            }
            if (height != NULL) {
                *height = jpeg_height;
            }
            
            ESP_LOGI(TAG, "Successfully read JPEG size: %dx%d", jpeg_width, jpeg_height);
            return jpeg_width > 0 && jpeg_height > 0;
        }

        if (fseek(file, segment_length - 2, SEEK_CUR) != 0) {
            ESP_LOGE(TAG, "display_get_jpg_size: fseek failed");
            break;
        }
    }

    fclose(file);
    return false;
}

bool display_draw_jpg_fit(const char *path, int *drawn_width, int *drawn_height) {
    if (drawn_width != NULL) {
        *drawn_width = 0;
    }
    if (drawn_height != NULL) {
        *drawn_height = 0;
    }

    if (!display_initialized || path == NULL) {
        ESP_LOGE(TAG, "display_draw_jpg_fit: display not initialized or path is NULL");
        return false;
    }

    int jpeg_width = 0;
    int jpeg_height = 0;
    if (!display_get_jpg_size(path, &jpeg_width, &jpeg_height)) {
        ESP_LOGE(TAG, "display_draw_jpg_fit: failed to get JPEG size from %s", path);
        return false;
    }

    ESP_LOGI(TAG, "JPEG dimensions: %dx%d", jpeg_width, jpeg_height);

    const int screen_width = tft.width();
    const int screen_height = tft.height();
    if (jpeg_width <= 0 || jpeg_height <= 0 || jpeg_width > screen_width * 8 || jpeg_height > screen_height * 8) {
        ESP_LOGE(TAG, "JPEG size check failed: %dx%d (screen %dx%d, limit %dx%d)",
                 jpeg_width, jpeg_height, screen_width, screen_height, screen_width * 8, screen_height * 8);
        return false;
    }

    // Hardware JPEG decoder only supports scales: 1.0, 0.5, 0.25, 0.125
    // Choose the best one that fits on screen (largest valid scale)
    float best_scale = 0.125f;
    const float valid_scales[] = {1.0f, 0.5f, 0.25f, 0.125f};

    for (int i = 0; i < 4; i++) {
        float test_scale = valid_scales[i];
        int scaled_w = (int)floorf((jpeg_width * test_scale) + 0.5f);
        int scaled_h = (int)floorf((jpeg_height * test_scale) + 0.5f);

        // Use the largest scale that fits on screen
        if (scaled_w <= screen_width && scaled_h <= screen_height) {
            best_scale = test_scale;
            ESP_LOGI(TAG, "Selected scale: %.3f (1/%d divisor) -> %dx%d", 
                     best_scale, (int)(1.0f / best_scale), scaled_w, scaled_h);
            break;
        }
    }

    int target_width = (int)floorf((jpeg_width * best_scale) + 0.5f);
    int target_height = (int)floorf((jpeg_height * best_scale) + 0.5f);
    if (target_width < 1) {
        target_width = 1;
    }
    if (target_height < 1) {
        target_height = 1;
    }

    int draw_x = (screen_width - target_width) / 2;
    int draw_y = (screen_height - target_height) / 2;

    ESP_LOGI(TAG, "Drawing JPEG at (%d, %d) size %dx%d with scale %.3f",
             draw_x, draw_y, target_width, target_height, (double)best_scale);

    // Open the file ourselves via POSIX to bypass LovyanGFX's DataWrapper
    // file-factory which sets need_transaction=true and may disrupt the
    // display SPI bus even though the SD card is on a completely separate bus.
    FILE *jpeg_fp = fopen(path, "rb");
    if (!jpeg_fp) {
        ESP_LOGE(TAG, "fopen failed for %s", path);
        return false;
    }

    PosixFileWrapper jpeg_wrapper(jpeg_fp);
    bool draw_ok = tft.drawJpg(&jpeg_wrapper, draw_x, draw_y, target_width, target_height, 0, 0, best_scale, 0.0f);
    fclose(jpeg_fp);

    if (!draw_ok) {
        ESP_LOGE(TAG, "tft.drawJpg failed for %s", path);
        return false;
    }

    ESP_LOGI(TAG, "JPEG rendered successfully");

    if (drawn_width != NULL) {
        *drawn_width = target_width;
    }
    if (drawn_height != NULL) {
        *drawn_height = target_height;
    }
    return true;
}

void display_measure_scaled_text(const char *text, int scale, int *width, int *height) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (!text || scale <= 0) return;

    int measured_width = (int)strlen(text) * disp_font_width * scale;
    int measured_height = disp_font_height * scale;

    if (width) *width = measured_width;
    if (height) *height = measured_height;
}

void display_draw_scaled_text_bg(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale) {
    if (!display_initialized || !text || scale <= 0) return;

    tft.setTextSize(scale);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(fg, bg);
    tft.drawString(text, x, y);
    tft.setTextSize(1);
}

void display_draw_char_at(int x, int y, char ch, uint16_t fg_color, uint16_t bg_color) {
    display_draw_unicode_at(x, y, (uint16_t)(uint8_t)ch, fg_color, bg_color);
}

static void codepoint_to_utf8(uint32_t cp, char *out, int *out_len) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        *out_len = 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        *out_len = 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        *out_len = 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        *out_len = 4;
    }
    out[*out_len] = '\0';
}

void display_draw_unicode_at(int x, int y, uint16_t codepoint, uint16_t fg_color, uint16_t bg_color) {
    if (!display_initialized) return;
    tft.fillRect(x, y, disp_font_width, disp_font_height, bg_color);
    if (codepoint == ' ') return;

    char text[5];
    int len;
    codepoint_to_utf8(codepoint, text, &len);

    int32_t clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
    tft.getClipRect(&clip_x, &clip_y, &clip_w, &clip_h);
    tft.setClipRect(x - 1, y, disp_font_width + 2, disp_font_height);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(fg_color, bg_color);
    tft.drawString(text, x, y);

    if (clip_w > 0 && clip_h > 0) {
        tft.setClipRect(clip_x, clip_y, clip_w, clip_h);
    } else {
        tft.clearClipRect();
    }
}

void display_draw_unicode_with_font(int x, int y, uint16_t codepoint, uint16_t fg_color, uint16_t bg_color, const uint8_t *font_data, size_t font_size) {
    if (!display_initialized || !font_data) return;
    tft.fillRect(x, y, disp_font_width, disp_font_height, bg_color);
    if (codepoint == ' ') return;

    tft.loadFont(font_data);

    char text[5];
    int len;
    codepoint_to_utf8(codepoint, text, &len);

    int32_t clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
    tft.getClipRect(&clip_x, &clip_y, &clip_w, &clip_h);
    tft.setClipRect(x - 1, y, disp_font_width + 2, disp_font_height);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(fg_color, bg_color);
    tft.drawString(text, x, y);

    if (clip_w > 0 && clip_h > 0) {
        tft.setClipRect(clip_x, clip_y, clip_w, clip_h);
    } else {
        tft.clearClipRect();
    }
}

int display_get_width(void) {
    if (!display_initialized || !display_tft) return SCREEN_WIDTH;
    return display_tft->width();
}

int display_get_height(void) {
    if (!display_initialized || !display_tft) return SCREEN_HEIGHT;
    return display_tft->height();
}

void display_set_rotation(int rotation) {
    if (rotation >= 0 && rotation <= 3) {
        current_rotation = rotation;
        if (display_initialized && display_tft) {
            display_tft->setRotation(rotation);
            ESP_LOGI(TAG, "Display rotation set to %d", rotation);

            // Recalculate text mode grid for new dimensions
            text_mode_reinit_grid();
        }
    }
}

void display_set_cursor(int x, int y) {
    if (display_tft) {
        display_tft->setCursor(x, y);
    }
}

void display_set_text_size(int size) {
    if (display_tft) {
        display_tft->setTextSize(size);
    }
}

void display_set_text_color(uint16_t color) {
    if (display_tft) {
        display_tft->setTextColor(color);
    }
}

void display_set_backlight(uint8_t brightness) {
    if (!display_initialized || !display_tft) return;
    display_tft->setBrightness(brightness);
}

void display_start_write(void) {
    if (!display_initialized || !display_tft) return;
    display_tft->startWrite();
}

void display_end_write(void) {
    if (!display_initialized || !display_tft) return;
    display_tft->endWrite();
}

void display_set_window(int x, int y, int w, int h) {
    if (!display_initialized || !display_tft) return;
    display_tft->setWindow(x, y, x + w - 1, y + h - 1);
}

void display_push_pixels(const uint16_t *data, int count) {
    if (!display_initialized || !display_tft || !data || count <= 0) return;
    display_tft->pushPixels(data, count);
}

void* display_create_sprite(int width, int height, int bpp) {
    if (!display_initialized || !display_tft) return NULL;
    LGFX_Sprite *sprite = new LGFX_Sprite(display_tft);
    sprite->setColorDepth(bpp);
    if (!sprite->createSprite(width, height)) {
        delete sprite;
        return NULL;
    }
    return sprite;
}

void sprite_set_palette_color(void *sprite_handle, int index, uint16_t rgb565) {
    if (!sprite_handle) return;
    LGFX_Sprite *sprite = static_cast<LGFX_Sprite*>(sprite_handle);
    sprite->setPaletteColor(index, rgb565);
}

void sprite_draw_pixel(void *sprite_handle, int x, int y, int color_index) {
    if (!sprite_handle) return;
    LGFX_Sprite *sprite = static_cast<LGFX_Sprite*>(sprite_handle);
    sprite->drawPixel(x, y, color_index);
}

void sprite_write_row(void *sprite_handle, int y, const uint8_t *indices, int width) {
    if (!sprite_handle || !indices || width <= 0) return;
    LGFX_Sprite *sprite = static_cast<LGFX_Sprite*>(sprite_handle);
    for (int x = 0; x < width; x++) {
        sprite->drawPixel(x, y, indices[x] & 0x03);
    }
}

static void *g_active_sprite = NULL;

void sprite_push(void *sprite_handle, int x, int y) {
    if (!sprite_handle) return;
    LGFX_Sprite *sprite = static_cast<LGFX_Sprite*>(sprite_handle);
    sprite->pushSprite(x, y);
}

void sprite_push_rotated_zoom(void *sprite_handle, int x, int y, float angle, float scale_x, float scale_y) {
    if (!sprite_handle) return;
    LGFX_Sprite *sprite = static_cast<LGFX_Sprite*>(sprite_handle);
    sprite->pushRotateZoom(x, y, angle, scale_x, scale_y);
}

void sprite_set_pivot(void *sprite_handle, float pivot_x, float pivot_y) {
    if (!sprite_handle) return;
    LGFX_Sprite *sprite = static_cast<LGFX_Sprite*>(sprite_handle);
    sprite->setPivot(pivot_x, pivot_y);
}

void sprite_destroy(void *sprite_handle) {
    if (!sprite_handle) return;
    LGFX_Sprite *sprite = static_cast<LGFX_Sprite*>(sprite_handle);
    if (g_active_sprite == sprite_handle) g_active_sprite = NULL;
    sprite->deleteSprite();
    delete sprite;
}

void sprite_set_active(void *sprite_handle) {
    g_active_sprite = sprite_handle;
}

void* sprite_get_active(void) {
    return g_active_sprite;
}

void led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
#if defined(BOARD_LED_RED_PIN)
    static bool led_initialized = false;
    if (!led_initialized) {
        ledc_timer_config_t timer = {};
        timer.speed_mode = BOARD_LED_LEDC_MODE;
        timer.duty_resolution = BOARD_LED_RESOLUTION;
        timer.timer_num = BOARD_LED_LEDC_TIMER;
        timer.freq_hz = BOARD_LED_FREQ;
        timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&timer);

        ledc_channel_config_t ch_red = {};
        ch_red.gpio_num = BOARD_LED_RED_PIN;
        ch_red.speed_mode = BOARD_LED_LEDC_MODE;
        ch_red.channel = BOARD_LED_CH_RED;
        ch_red.timer_sel = BOARD_LED_LEDC_TIMER;
        ledc_channel_config(&ch_red);

        ledc_channel_config_t ch_green = {};
        ch_green.gpio_num = BOARD_LED_GREEN_PIN;
        ch_green.speed_mode = BOARD_LED_LEDC_MODE;
        ch_green.channel = BOARD_LED_CH_GREEN;
        ch_green.timer_sel = BOARD_LED_LEDC_TIMER;
        ledc_channel_config(&ch_green);

        ledc_channel_config_t ch_blue = {};
        ch_blue.gpio_num = BOARD_LED_BLUE_PIN;
        ch_blue.speed_mode = BOARD_LED_LEDC_MODE;
        ch_blue.channel = BOARD_LED_CH_BLUE;
        ch_blue.timer_sel = BOARD_LED_LEDC_TIMER;
        ledc_channel_config(&ch_blue);

        led_initialized = true;
    }

    ledc_set_duty(BOARD_LED_LEDC_MODE, BOARD_LED_CH_RED,   r);
    ledc_set_duty(BOARD_LED_LEDC_MODE, BOARD_LED_CH_GREEN, g);
    ledc_set_duty(BOARD_LED_LEDC_MODE, BOARD_LED_CH_BLUE,  b);
    ledc_update_duty(BOARD_LED_LEDC_MODE, BOARD_LED_CH_RED);
    ledc_update_duty(BOARD_LED_LEDC_MODE, BOARD_LED_CH_GREEN);
    ledc_update_duty(BOARD_LED_LEDC_MODE, BOARD_LED_CH_BLUE);
#else
    (void)r; (void)g; (void)b;
#endif
}

void keyboard_set_backlight(uint8_t brightness) {
#if BOARD_HAS_KEYBOARD_BACKLIGHT
    bbq20_set_backlight(brightness);
#else
    (void)brightness;
#endif
}

uint8_t keyboard_get_backlight(void) {
#if BOARD_HAS_KEYBOARD_BACKLIGHT
    return bbq20_get_backlight();
#else
    return 0;
#endif
}

static const board_info_t board_info = {
    .board_name = BOARD_NAME,
    .screen_width = BOARD_SCREEN_WIDTH,
    .screen_height = BOARD_SCREEN_HEIGHT,
    .has_touchscreen = BOARD_HAS_TOUCHSCREEN,
    .has_keyboard_backlight = BOARD_HAS_KEYBOARD_BACKLIGHT,
};

const board_info_t *os_get_board_info(void) {
    return &board_info;
}

bool display_load_vlw_font(const char *path) {
    if (!display_tft) {
        ESP_LOGE(TAG, "display_tft is null, cannot load font");
        return false;
    }

    ESP_LOGI(TAG, "Attempting to load VLW font: %s", path);

    // Try to load the VLW font from SD card
    bool success = display_tft->loadFont(path);

    if (success) {
        ESP_LOGI(TAG, "Successfully loaded VLW font: %s", path);
    } else {
        ESP_LOGE(TAG, "Failed to load VLW font: %s", path);
        ESP_LOGE(TAG, "This might be because LovyanGFX needs a file system object");
        ESP_LOGE(TAG, "or the file doesn't exist on the SD card");
    }
    return success;
}

int display_get_rotation(void) {
    return current_rotation;
}

void display_apply_saved_rotation(void) {
    int saved_rotation = os_settings_get_int("display/rotation", DEFAULT_DISPLAY_ROTATION);
    ESP_LOGI(TAG, "Applying saved rotation setting: %d", saved_rotation);
    display_set_rotation(saved_rotation);
}

void display_apply_saved_backlight(void) {
    int saved_brightness = os_settings_get_int("display/backlight", 255);
    if (saved_brightness < 0) saved_brightness = 0;
    if (saved_brightness > 255) saved_brightness = 255;
    ESP_LOGI(TAG, "Applying saved backlight brightness: %d", saved_brightness);
    display_set_backlight((uint8_t)saved_brightness);
}

void transform_touch_coordinates(int *x, int *y, int rotation) {
    if (!x || !y) return;

    int original_x = *x;
    int original_y = *y;
    const int width = BOARD_SCREEN_WIDTH;
    const int height = BOARD_SCREEN_HEIGHT;

    switch (rotation) {
        case 0: // 0° - no transformation
            *x = original_x;
            *y = original_y;
            break;
        case 1: // 90° clockwise
            *x = height - original_y;
            *y = original_x;
            break;
        case 2: // 180°
            *x = width - original_x;
            *y = height - original_y;
            break;
        case 3: // 270° clockwise (or 90° counter-clockwise)
            *x = original_y;
            *y = width - original_x;
            break;
        default:
            // Default to no transformation for invalid rotation values
            *x = original_x;
            *y = original_y;
            break;
    }
}

bool display_save_screenshot_ppm(const char *path) {
    if (!path || !display_initialized || !display_tft) return false;
    if (!sd_card_is_mounted()) return false;

    FILE *fppm = fopen(path, "wb");
    if (!fppm) return false;

    const int width = display_get_width();
    const int height = display_get_height();
    fprintf(fppm, "P6\n%d %d\n255\n", width, height);

    uint8_t *row_buf = (uint8_t *)malloc(width * 3);
    if (!row_buf) {
        fclose(fppm);
        return false;
    }

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
        fwrite(row_buf, 1, width * 3, fppm);
    }

    free(row_buf);
    fclose(fppm);
    return true;
}

// Keyboard implementation for BBQ20 (based on terminado)
bool keyboard_init(void) {
#if BOARD_HAS_BBQ20_KEYBOARD
    // Idempotent: boot.cpp and hardware_init() both call this; avoid
    // re-initializing the I2C bus and BBQ20 controller twice in one boot.
    if (keyboard_initialized) return true;

    ESP_LOGI(TAG, "Initializing BBQ20 keyboard driver");

    if (bbq20_keyboard_init()) {
        keyboard_initialized = true;
        ESP_LOGI(TAG, "BBQ20 keyboard driver ready");
        return true;
    } else {
        ESP_LOGE(TAG, "BBQ20 keyboard initialization failed");
        return false;
    }
#else
    ESP_LOGI(TAG, "No keyboard on this board");
    return false;
#endif
}

bool keyboard_is_available(void) {
    return keyboard_initialized;
}

void keyboard_deinit(void) {
#if BOARD_HAS_BBQ20_KEYBOARD
    bbq20_keyboard_deinit();
#endif
    keyboard_initialized = false;
}

bool keyboard_read_event(event_t *event) {
#if BOARD_HAS_BBQ20_KEYBOARD
    static bool first_call = true;

    if (first_call) {
        ESP_LOGI(TAG, "BBQ20 keyboard polling started");
        first_call = false;
    }

    bbq20_key_event_t bbq20_event;

    if (bbq20_read_key_event(&bbq20_event)) {
        event->type = EVENT_KEYBOARD;
        event->keyboard.key = (char)bbq20_event.key_code;
        event->keyboard.pressed = bbq20_event.pressed;
        event->keyboard.modifiers = bbq20_event.modifiers;
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
#endif
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
    esp_err_t ret = uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
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

static const esp_partition_t *rom_part = NULL;
static esp_partition_mmap_handle_t rom_mmap_handle;
static const uint8_t *rom_flash_data = NULL;
static size_t rom_flash_size = 0;

const uint8_t* flash_rom_load(const char *path, size_t *out_size) {
    flash_rom_unload();

    rom_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                         ESP_PARTITION_SUBTYPE_DATA_UNDEFINED,
                                         "app_code");
    if (!rom_part) {
        ESP_LOGE(TAG, "flash_rom_load: app_code partition not found");
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "flash_rom_load: can't open %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (rom_size < 0x150) {
        ESP_LOGE(TAG, "flash_rom_load: ROM too small (%d bytes)", rom_size);
        fclose(f);
        return NULL;
    }

    size_t aligned_size = (rom_size + 0xFFF) & ~0xFFF;
    uint32_t rom_offset = (rom_part->size - aligned_size) & ~0xFFF;

    if (aligned_size > rom_part->size) {
        ESP_LOGE(TAG, "flash_rom_load: ROM too large (%d > partition %d)",
                 rom_size, rom_part->size);
        fclose(f);
        return NULL;
    }

    ESP_LOGI(TAG, "flash_rom_load: %d bytes at partition offset 0x%x (partition size %d)",
             rom_size, rom_offset, rom_part->size);

    esp_err_t err = esp_partition_erase_range(rom_part, rom_offset, aligned_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flash_rom_load: erase failed: %s", esp_err_to_name(err));
        fclose(f);
        return NULL;
    }

    uint8_t buf[4096];
    size_t written = 0;
    while (written < rom_size) {
        size_t to_read = rom_size - written;
        if (to_read > sizeof(buf)) to_read = sizeof(buf);
        size_t got = fread(buf, 1, to_read, f);
        if (got == 0) break;
        err = esp_partition_write(rom_part, rom_offset + written, buf, got);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "flash_rom_load: write failed at %d: %s", written, esp_err_to_name(err));
            fclose(f);
            return NULL;
        }
        written += got;
    }
    fclose(f);

    ESP_LOGI(TAG, "flash_rom_load: wrote %d/%d bytes to flash", written, rom_size);

    const void *ptr;
    err = esp_partition_mmap(rom_part, rom_offset, aligned_size,
                              ESP_PARTITION_MMAP_DATA, &ptr, &rom_mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flash_rom_load: mmap failed: %s", esp_err_to_name(err));
        return NULL;
    }

    rom_flash_data = (const uint8_t*)ptr;
    rom_flash_size = rom_size;
    if (out_size) *out_size = rom_size;

    ESP_LOGI(TAG, "flash_rom_load: mapped at %p, size %d", rom_flash_data, rom_flash_size);
    return rom_flash_data;
}

void flash_rom_unload(void) {
    if (rom_flash_data) {
        ESP_LOGI(TAG, "flash_rom_unload: unmapping ROM");
        esp_partition_munmap(rom_mmap_handle);
        rom_flash_data = NULL;
        rom_flash_size = 0;
    }
}
