#ifndef GRAPHICS_MODE_H
#define GRAPHICS_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void graphics_mode_init(uint8_t *buffer, size_t buffer_size);
void graphics_set_palette(const uint16_t *colors, int count);
void graphics_clear(uint8_t color);
void graphics_draw_pixel(int x, int y, uint8_t color);
void graphics_draw_line(int x0, int y0, int x1, int y1, uint8_t color);
void graphics_fill_rect(int x, int y, int w, int h, uint8_t color);
void graphics_draw_rect(int x, int y, int w, int h, uint8_t color);
void graphics_draw_string(int x, int y, const char *text, uint8_t color);
void graphics_flush(void);
void *graphics_mode_get_buffer(void);
size_t graphics_mode_get_buffer_size(void);
void graphics_mode_deinit(void);
bool graphics_mode_is_active(void);
bool graphics_mode_save_screenshot(void);
void graphics_blit_scaled(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);

#endif
