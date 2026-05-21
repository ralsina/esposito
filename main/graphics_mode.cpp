#include "graphics_mode.h"
#include "hardware.h"
#include "lovgfx_config.h"
#include <lgfx/v1/LGFXBase.hpp>
#include <string.h>
#include <algorithm>

extern LGFX* display_tft;

// Default CGA-style 16-color palette (RGB565), matching text_mode
static const uint16_t default_palette[16] = {
    0x0000, 0x0010, 0x0400, 0x0410, 0x8000, 0x8010, 0x8400, 0x8410,
    0x4208, 0x001F, 0x07E0, 0x07FF, 0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

// Screen dimensions (fixed for CYD)
static const int SCREEN_WIDTH = 320;
static const int SCREEN_HEIGHT = 240;

// Graphics mode state
static uint8_t *g_buffer = NULL;
static size_t g_buffer_size = 0;
static uint16_t g_palette[16];
static bool g_active = false;

// Helper: set a 4-bit nibble in the buffer
static void set_nibble(int x, int y, uint8_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    if (!g_buffer) return;

    int index = (y * SCREEN_WIDTH + x) / 2;
    bool is_low = (x % 2) == 0;

    if (is_low) {
        g_buffer[index] = (g_buffer[index] & 0x0F) | ((color & 0x0F) << 4);
    } else {
        g_buffer[index] = (g_buffer[index] & 0xF0) | (color & 0x0F);
    }
}

void graphics_mode_init(uint8_t *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;

    size_t required = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT / 2;
    if (buffer_size < required) return;

    g_buffer = buffer;
    g_buffer_size = buffer_size;
    g_active = true;

    // Copy default palette
    memcpy(g_palette, default_palette, sizeof(g_palette));

    // Clear to black (palette index 0)
    memset(g_buffer, 0, required);

    // Push cleared buffer to display immediately
    graphics_flush();
}

void graphics_set_palette(const uint16_t *colors, int count) {
    if (!colors || count <= 0 || !g_active) return;
    if (count > 16) count = 16;
    memcpy(g_palette, colors, count * sizeof(uint16_t));
}

void graphics_clear(uint8_t color) {
    if (!g_buffer || !g_active) return;
    color &= 0x0F;

    // Fill with the color in both nibbles
    uint8_t fill = (color << 4) | color;
    size_t required = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT / 2;
    memset(g_buffer, fill, required);
}

void graphics_draw_pixel(int x, int y, uint8_t color) {
    if (!g_active) return;
    set_nibble(x, y, color & 0x0F);
}

void graphics_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    if (!g_active) return;
    color &= 0x0F;

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        set_nibble(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void graphics_fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (!g_buffer || !g_active) return;
    color &= 0x0F;

    // Clamp to screen bounds
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || w <= 0 || h <= 0) return;
    if (x + w <= 0 || y + h <= 0) return;

    int x_start = std::max(0, x);
    int y_start = std::max(0, y);
    int x_end = std::min(SCREEN_WIDTH, x + w);
    int y_end = std::min(SCREEN_HEIGHT, y + h);

    uint8_t fill = (color << 4) | color;

    for (int row = y_start; row < y_end; row++) {
        int row_start = row * SCREEN_WIDTH + x_start;
        int row_end = row * SCREEN_WIDTH + x_end;

        // Handle odd start position
        if (row_start % 2 != 0) {
            int index = row_start / 2;
            g_buffer[index] = (g_buffer[index] & 0xF0) | color;
            row_start++;
        }

        // Handle odd end position
        if (row_end % 2 != 0) {
            int index = (row_end - 1) / 2;
            g_buffer[index] = (g_buffer[index] & 0x0F) | (color << 4);
            row_end--;
        }

        // Fill even-aligned middle with memset
        if (row_end > row_start) {
            int start_index = row_start / 2;
            int end_index = row_end / 2;
            memset(g_buffer + start_index, fill, end_index - start_index);
        }
    }
}

void graphics_draw_rect(int x, int y, int w, int h, uint8_t color) {
    if (!g_active || w <= 0 || h <= 0) return;
    color &= 0x0F;

    graphics_draw_line(x, y, x + w - 1, y, color);
    graphics_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    graphics_draw_line(x, y, x, y + h - 1, color);
    graphics_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void graphics_flush(void) {
    if (!g_buffer || !g_active) return;

    // Convert RGB565 palette to lgfx::rgb565_t for LovyanGFX
    lgfx::rgb565_t palette[16];
    for (int i = 0; i < 16; i++) {
        palette[i] = g_palette[i];
    }

    display_tft->pushImage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                           g_buffer, lgfx::palette_4bit, palette);
}

void graphics_mode_deinit(void) {
    g_buffer = NULL;
    g_buffer_size = 0;
    g_active = false;
}

bool graphics_mode_is_active(void) {
    return g_active;
}
