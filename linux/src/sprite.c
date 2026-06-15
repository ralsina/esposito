#include "hardware.h"
#include "text_mode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SDL2/SDL.h>

typedef struct {
    int width, height, bpp;
    uint8_t *pixels;
    int stride;         // bytes per row
    uint16_t palette[256];
    int palette_size;
    float pivot_x, pivot_y;
    bool active;
} sprite_t;

static sprite_t *active_sprite = NULL;
static SDL_mutex *render_mutex = NULL;

static void ensure_mutex(void) {
    if (!render_mutex)
        render_mutex = SDL_CreateMutex();
}

static sprite_t *get_sprite(void *handle) {
    return (sprite_t *)handle;
}

static void sprite_set_pixel(sprite_t *s, int x, int y, int color) {
    if (!s || !s->pixels || x < 0 || x >= s->width || y < 0 || y >= s->height)
        return;
    color &= (1 << s->bpp) - 1;

    switch (s->bpp) {
        case 1: {
            int idx = y * s->stride + x / 8;
            int shift = 7 - (x % 8);
            s->pixels[idx] = (s->pixels[idx] & ~(1 << shift)) | ((color & 1) << shift);
            break;
        }
        case 2: {
            int idx = y * s->stride + x / 4;
            int shift = (3 - (x % 4)) * 2;
            s->pixels[idx] = (s->pixels[idx] & ~(3 << shift)) | ((color & 3) << shift);
            break;
        }
        case 4: {
            int idx = y * s->stride + x / 2;
            int shift = (x & 1) ? 0 : 4;
            s->pixels[idx] = (s->pixels[idx] & ~(0xF << shift)) | ((color & 0xF) << shift);
            break;
        }
        case 8:
            s->pixels[y * s->stride + x] = color & 0xFF;
            break;
    }
}

static int sprite_get_pixel(sprite_t *s, int x, int y) {
    if (!s || !s->pixels || x < 0 || x >= s->width || y < 0 || y >= s->height)
        return 0;
    switch (s->bpp) {
        case 1: {
            int idx = y * s->stride + x / 8;
            return (s->pixels[idx] >> (7 - (x % 8))) & 1;
        }
        case 2: {
            int idx = y * s->stride + x / 4;
            return (s->pixels[idx] >> ((3 - (x % 4)) * 2)) & 3;
        }
        case 4: {
            int idx = y * s->stride + x / 2;
            return (s->pixels[idx] >> ((x & 1) ? 0 : 4)) & 0xF;
        }
        case 8:
            return s->pixels[y * s->stride + x];
        default:
            return 0;
    }
}

void *display_create_sprite(int width, int height, int bpp) {
    sprite_t *s = calloc(1, sizeof(sprite_t));
    if (!s) return NULL;
    s->width = width;
    s->height = height;
    s->bpp = bpp;
    s->stride = (width * bpp + 7) / 8;
    s->pixels = calloc(1, s->stride * height);
    if (!s->pixels) {
        free(s);
        return NULL;
    }
    return s;
}

void sprite_set_palette_color(void *handle, int index, uint16_t rgb565) {
    sprite_t *s = get_sprite(handle);
    if (!s || index < 0 || index >= 256) return;
    s->palette[index] = rgb565;
    if (index + 1 > s->palette_size)
        s->palette_size = index + 1;
}

void sprite_draw_pixel(void *handle, int x, int y, int color_index) {
    sprite_set_pixel(get_sprite(handle), x, y, color_index);
}

void sprite_write_row(void *handle, int y, const uint8_t *indices, int width) {
    sprite_t *s = get_sprite(handle);
    if (!s || !indices || y < 0 || y >= s->height) return;
    if (width > s->width) width = s->width;
    for (int x = 0; x < width; x++)
        sprite_set_pixel(s, x, y, indices[x]);
}

static uint32_t rgb565_to_argb8888(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F) << 3;
    uint8_t g = ((c >> 5) & 0x3F) << 2;
    uint8_t b = (c & 0x1F) << 3;
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

void sprite_push(void *handle, int x, int y) {
    sprite_push_rotated_zoom(handle, x, y, 0.0f, 1.0f, 1.0f);
}

void sprite_push_rotated_zoom(void *handle, int x, int y, float angle, float scale_x, float scale_y) {
    sprite_t *s = get_sprite(handle);
    if (!s) return;
    (void)angle; // ignore rotation

    int w = (int)(s->width * scale_x);
    int h = (int)(s->height * scale_y);
    if (w <= 0 || h <= 0) return;

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return;

    SDL_LockSurface(surf);
    uint32_t *dst = (uint32_t *)surf->pixels;

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int sx = (int)(col / scale_x);
            int sy = (int)(row / scale_y);
            int idx = sprite_get_pixel(s, sx, sy);
            int max_c = 1 << s->bpp;
            if (max_c > s->palette_size) max_c = s->palette_size;
            if (max_c < 1) max_c = 1;
            dst[row * w + col] = rgb565_to_argb8888(s->palette[idx % max_c]);
        }
    }
    SDL_UnlockSurface(surf);

    ensure_mutex();
    SDL_LockMutex(render_mutex);
    SDL_Renderer *renderer = text_mode_get_renderer();
    if (!renderer) { SDL_UnlockMutex(render_mutex); SDL_FreeSurface(surf); return; }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) { SDL_UnlockMutex(render_mutex); return; }

    SDL_Rect dst_rect = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, tex, NULL, &dst_rect);
    SDL_RenderPresent(renderer);
    SDL_DestroyTexture(tex);
    SDL_UnlockMutex(render_mutex);
}

void sprite_set_pivot(void *handle, float pivot_x, float pivot_y) {
    sprite_t *s = get_sprite(handle);
    if (!s) return;
    s->pivot_x = pivot_x;
    s->pivot_y = pivot_y;
}

void sprite_destroy(void *handle) {
    sprite_t *s = get_sprite(handle);
    if (!s) return;
    ensure_mutex();
    SDL_LockMutex(render_mutex);
    if (active_sprite == s) active_sprite = NULL;
    SDL_UnlockMutex(render_mutex);
    free(s->pixels);
    free(s);
}

void sprite_set_active(void *handle) {
    active_sprite = get_sprite(handle);
}

void *sprite_get_active(void) {
    return active_sprite;
}
