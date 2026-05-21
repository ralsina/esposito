#include "graphics_mode.h"
#include "hardware.h"
#include "lovgfx_config.h"
#include <lgfx/v1/LGFXBase.hpp>
#include <lgfx/v1/LGFX_Sprite.hpp>
#include <esp_log.h>
#include <string.h>
#include <algorithm>

static const char *TAG = "graphics_mode";

extern LGFX* display_tft;

// Default CGA-style 16-color palette (RGB565), matching text_mode
static const uint16_t default_palette[16] = {
    0x0000, 0x0010, 0x0400, 0x0410, 0x8000, 0x8010, 0x8400, 0x8410,
    0x4208, 0x001F, 0x07E0, 0x07FF, 0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

// Screen dimensions (fixed for CYD)
static const int SCREEN_WIDTH = 320;
static const int SCREEN_HEIGHT = 240;

// Sprite instance (static to avoid allocation overhead)
static LGFX_Sprite *g_sprite = NULL;
static bool g_active = false;

void graphics_mode_init(uint8_t *buffer, size_t buffer_size) {
    ESP_LOGI(TAG, "graphics_mode_init: entering, buffer=%p size=%u", buffer, buffer_size);

    size_t required = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT / 2;
    if (!buffer || buffer_size < required) {
        ESP_LOGE(TAG, "graphics_mode_init: buffer too small (need %u, got %u)", required, buffer_size);
        return;
    }

    if (g_sprite) {
        ESP_LOGI(TAG, "graphics_mode_init: deleting existing sprite");
        g_sprite->deleteSprite();
        delete g_sprite;
        g_sprite = NULL;
    }

    ESP_LOGI(TAG, "graphics_mode_init: allocating LGFX_Sprite (free heap: %lu)", esp_get_free_heap_size());
    g_sprite = new LGFX_Sprite(display_tft);
    if (!g_sprite) {
        ESP_LOGE(TAG, "graphics_mode_init: new LGFX_Sprite failed");
        return;
    }

    g_sprite->setColorDepth(4);
    ESP_LOGI(TAG, "graphics_mode_init: setBuffer %dx%d bpp=4", SCREEN_WIDTH, SCREEN_HEIGHT);
    g_sprite->setBuffer(buffer, SCREEN_WIDTH, SCREEN_HEIGHT, 4);

    // Create palette for 4bpp mode
    g_sprite->createPalette();

    g_active = true;

    // Set default palette
    for (int i = 0; i < 16; i++) {
        g_sprite->setPaletteColor(i, default_palette[i]);
    }

    // Clear to black (palette index 0)
    g_sprite->fillSprite(0);
    ESP_LOGI(TAG, "graphics_mode_init: flushing to display");
    graphics_flush();
    ESP_LOGI(TAG, "graphics_mode_init: done, active=%d", g_active);
}

void graphics_mode_deinit(void) {
    display_clear(0x0000);

    if (g_sprite) {
        g_sprite->deleteSprite();
        delete g_sprite;
        g_sprite = NULL;
    }
    g_active = false;
}

void graphics_set_palette(const uint16_t *colors, int count) {
    if (!colors || count <= 0 || !g_active || !g_sprite) return;
    if (count > 16) count = 16;
    for (int i = 0; i < count; i++) {
        g_sprite->setPaletteColor(i, colors[i]);
    }
}

void graphics_clear(uint8_t color) {
    if (!g_active || !g_sprite) return;
    g_sprite->fillSprite(color & 0x0F);
}

void graphics_draw_pixel(int x, int y, uint8_t color) {
    if (!g_active || !g_sprite) return;
    g_sprite->drawPixel(x, y, color & 0x0F);
}

void graphics_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    if (!g_active || !g_sprite) return;
    g_sprite->drawLine(x0, y0, x1, y1, color & 0x0F);
}

void graphics_fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (!g_active || !g_sprite) return;
    g_sprite->fillRect(x, y, w, h, color & 0x0F);
}

void graphics_draw_rect(int x, int y, int w, int h, uint8_t color) {
    if (!g_active || !g_sprite) return;
    g_sprite->drawRect(x, y, w, h, color & 0x0F);
}

void graphics_draw_string(int x, int y, const char *text, uint8_t color) {
    if (!g_active || !g_sprite) return;
    g_sprite->setTextColor(color & 0x0F);
    g_sprite->setCursor(x, y);
    g_sprite->print(text);
}

void graphics_flush(void) {
    if (!g_active || !g_sprite) return;
    g_sprite->pushSprite(display_tft, 0, 0);
}

void *graphics_mode_get_buffer(void) {
    if (!g_sprite) return NULL;
    return g_sprite->getBuffer();
}

size_t graphics_mode_get_buffer_size(void) {
    return (size_t)SCREEN_WIDTH * SCREEN_HEIGHT / 2;
}

bool graphics_mode_is_active(void) {
    return g_active;
}
