#ifndef GRAPHICS_MODE_H
#define GRAPHICS_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize graphics mode with a caller-provided buffer.
// buffer_size must be at least (width * height / 2) bytes for 4bpp.
// For the standard 320x240 display this is 38,400 bytes.
// The app owns the buffer allocation and lifetime.
void graphics_mode_init(uint8_t *buffer, size_t buffer_size);

// Set the 16-color palette (RGB565 values).
void graphics_set_palette(const uint16_t *colors, int count);

// Clear the sprite to the given palette index (0-15).
void graphics_clear(uint8_t color);

// Draw a single pixel at (x, y) with the given palette index (0-15).
void graphics_draw_pixel(int x, int y, uint8_t color);

// Draw a line from (x0, y0) to (x1, y1) with the given palette index (0-15).
void graphics_draw_line(int x0, int y0, int x1, int y1, uint8_t color);

// Fill a rectangle with the given palette index (0-15).
void graphics_fill_rect(int x, int y, int w, int h, uint8_t color);

// Draw a rectangle outline with the given palette index (0-15).
void graphics_draw_rect(int x, int y, int w, int h, uint8_t color);

// Draw text using LovyanGFX's font renderer.
// color is a palette index (0-15).
void graphics_draw_string(int x, int y, const char *text, uint8_t color);

// Blit the sprite to the display.
void graphics_flush(void);

// Get the sprite's raw buffer pointer (for direct read/write, e.g. save/load).
void *graphics_mode_get_buffer(void);

// Get the sprite buffer size in bytes.
size_t graphics_mode_get_buffer_size(void);

// Deactivate graphics mode and free sprite. Does NOT free the buffer (app owns it).
void graphics_mode_deinit(void);

// Returns true if graphics mode is currently active.
bool graphics_mode_is_active(void);

// Save current graphics buffer as PPM screenshot. Returns true on success.
bool graphics_mode_save_screenshot(void);

// Blit a 4bpp source buffer scaled by an integer factor into the active sprite.
// src_w/src_h are the source dimensions, dst_x/dst_y are the destination offset, scale is the integer scale factor.
void graphics_blit_scaled(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);

#ifdef __cplusplus
}
#endif

#endif // GRAPHICS_MODE_H
