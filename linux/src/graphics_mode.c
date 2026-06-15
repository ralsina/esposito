#include "graphics_mode.h"
#include "text_mode.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>

#define FB_W 320
#define FB_H 240

static uint8_t *fb = NULL;
static size_t fb_size = 0;
static uint16_t palette[16];
static int palette_count = 0;
static bool active = false;

// Pending text commands (rendered during flush via VLW font)
#define MAX_TEXTS 64
static struct { int x, y; char str[64]; uint8_t color; } pending_texts[MAX_TEXTS];
static int pending_text_count = 0;

static uint8_t get_pixel(int x, int y) {
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H || !fb) return 0;
    uint8_t byte = fb[(y * FB_W + x) / 2];
    if (x & 1) return byte & 0x0F;
    return byte >> 4;
}

static void set_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H || !fb) return;
    int idx = (y * FB_W + x) / 2;
    uint8_t byte = fb[idx];
    if (x & 1) byte = (byte & 0xF0) | (color & 0x0F);
    else byte = (color << 4) | (byte & 0x0F);
    fb[idx] = byte;
}

static uint32_t rgb565_to_argb(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F) << 3;
    uint8_t g = ((c >> 5) & 0x3F) << 2;
    uint8_t b = (c & 0x1F) << 3;
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

void graphics_mode_init(uint8_t *buffer, size_t buffer_size) {
    fb = buffer;
    fb_size = buffer_size;
    active = true;
    pending_text_count = 0;
    // Default grayscale palette if none set yet
    if (palette_count == 0) {
        for (int i = 0; i < 16; i++) {
            uint8_t v = (i * 255) / 15;
            uint16_t r5 = v >> 3;
            uint16_t g6 = v >> 2;
            uint16_t b5 = v >> 3;
            palette[i] = (r5 << 11) | (g6 << 5) | b5;
        }
        palette_count = 16;
    }
}

void graphics_set_palette(const uint16_t *colors, int count) {
    if (count > 16) count = 16;
    for (int i = 0; i < count; i++)
        palette[i] = colors[i];
    palette_count = count;
}

void graphics_clear(uint8_t color) {
    if (!fb) return;
    uint8_t c = (color & 0x0F);
    uint8_t byte = (c << 4) | c;
    memset(fb, byte, fb_size);
}

void graphics_draw_pixel(int x, int y, uint8_t color) {
    set_pixel(x, y, color);
}

static void bresenham_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void graphics_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    bresenham_line(x0, y0, x1, y1, color);
}

void graphics_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            set_pixel(col, row, color);
}

void graphics_draw_rect(int x, int y, int w, int h, uint8_t color) {
    for (int col = x; col < x + w; col++) {
        set_pixel(col, y, color);
        set_pixel(col, y + h - 1, color);
    }
    for (int row = y; row < y + h; row++) {
        set_pixel(x, row, color);
        set_pixel(x + w - 1, row, color);
    }
}

void graphics_draw_string(int x, int y, const char *text, uint8_t color) {
    if (pending_text_count < MAX_TEXTS) {
        int idx = pending_text_count++;
        pending_texts[idx].x = x;
        pending_texts[idx].y = y;
        pending_texts[idx].color = color;
        strncpy(pending_texts[idx].str, text, sizeof(pending_texts[idx].str) - 1);
        pending_texts[idx].str[sizeof(pending_texts[idx].str) - 1] = '\0';
    }
}

void graphics_flush(void) {
    if (!fb || !active) return;

    // Convert 4bpp indexed buffer to ARGB surface
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, FB_W, FB_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return;

    SDL_LockSurface(surf);
    uint32_t *pixels = (uint32_t *)surf->pixels;
    for (int y = 0; y < FB_H; y++) {
        for (int x = 0; x < FB_W; x++) {
            uint8_t idx = get_pixel(x, y);
            pixels[y * FB_W + x] = rgb565_to_argb(palette[idx & 0x0F]);
        }
    }
    SDL_UnlockSurface(surf);

    SDL_Renderer *renderer = text_mode_get_renderer();
    if (!renderer) {
        SDL_FreeSurface(surf);
        return;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, tex, NULL, NULL);
    SDL_DestroyTexture(tex);

    // Render pending text via VLW font
    for (int i = 0; i < pending_text_count; i++) {
        uint16_t c = palette[pending_texts[i].color & 0x0F];
        uint8_t r = ((c >> 11) & 0x1F) << 3;
        uint8_t g = ((c >> 5) & 0x3F) << 2;
        uint8_t b = (c & 0x1F) << 3;
        text_mode_render_string(pending_texts[i].x, pending_texts[i].y,
                                pending_texts[i].str, r, g, b);
    }
    pending_text_count = 0;

    SDL_RenderPresent(renderer);
}

void *graphics_mode_get_buffer(void) {
    return fb;
}

size_t graphics_mode_get_buffer_size(void) {
    return fb_size;
}

void graphics_mode_deinit(void) {
    active = false;
    fb = NULL;
    fb_size = 0;
    pending_text_count = 0;
}

bool graphics_mode_is_active(void) {
    return active;
}

bool graphics_mode_save_screenshot(void) {
    // Not implemented for emulator
    return false;
}

void graphics_blit_scaled(const uint8_t *src, int src_w, int src_h,
                          int dst_x, int dst_y, int scale) {
    if (!fb) return;
    for (int sy = 0; sy < src_h; sy++) {
        for (int sx = 0; sx < src_w; sx++) {
            uint8_t byte = src[(sy * src_w + sx) / 2];
            uint8_t idx = (sx & 1) ? (byte & 0x0F) : (byte >> 4);
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    set_pixel(dst_x + sx * scale + dx,
                              dst_y + sy * scale + dy, idx);
                }
            }
        }
    }
}
