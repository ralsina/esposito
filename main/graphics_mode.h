#ifndef GRAPHICS_MODE_H
#define GRAPHICS_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize graphics mode with an app-provided buffer.
// buffer_size must be at least (width * height / 2) bytes for 4bpp.
// For the standard 320x240 display this is 38,400 bytes.
// The app owns the buffer allocation and lifetime.
void graphics_mode_init(uint8_t *buffer, size_t buffer_size);

// Set the 16-color palette (RGB565 values).
// If count < 16, remaining entries keep their previous values.
// Default is the CGA-style palette matching text_mode colors.
void graphics_set_palette(const uint16_t *colors, int count);

// Clear the entire sprite buffer to the given palette index (0-15).
void graphics_clear(uint8_t color);

// Draw a single pixel at (x, y) with the given palette index (0-15).
void graphics_draw_pixel(int x, int y, uint8_t color);

// Draw a line from (x0, y0) to (x1, y1) with the given palette index (0-15).
void graphics_draw_line(int x0, int y0, int x1, int y1, uint8_t color);

// Fill a rectangle with the given palette index (0-15).
void graphics_fill_rect(int x, int y, int w, int h, uint8_t color);

// Draw a rectangle outline with the given palette index (0-15).
void graphics_draw_rect(int x, int y, int w, int h, uint8_t color);

// Blit the entire sprite to the display.
// Palette conversion to RGB565 happens on-the-fly during the SPI push.
void graphics_flush(void);

// Deactivate graphics mode. Does NOT free the buffer (app owns it).
void graphics_mode_deinit(void);

// Returns true if graphics mode is currently active.
bool graphics_mode_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // GRAPHICS_MODE_H
