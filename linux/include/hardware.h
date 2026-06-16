#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Display (mostly stubs — apps use text_mode for rendering)
void display_clear(uint16_t color);
void display_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void display_draw_text(int x, int y, const char *text, uint16_t color);
void display_draw_text_transparent(int x, int y, const char *text, uint16_t color);
void display_draw_text_bg(int x, int y, const char *text, uint16_t fg, uint16_t bg);
void display_draw_char_at(int x, int y, char ch, uint16_t fg_color, uint16_t bg_color);
void display_get_jpg_size(const char *path, uint16_t *w, uint16_t *h);
bool display_draw_jpg_fit(const char *path, uint16_t x, uint16_t y);
int display_get_width(void);
int display_get_height(void);
void display_set_backlight(uint8_t brightness);

// Sprite API
void *display_create_sprite(int width, int height, int bpp);
void sprite_set_palette_color(void *sprite, int index, uint16_t rgb565);
void sprite_draw_pixel(void *sprite, int x, int y, int color_index);
void sprite_write_row(void *sprite, int y, const uint8_t *indices, int width);
void sprite_push(void *sprite, int x, int y);
void sprite_push_rotated_zoom(void *sprite, int x, int y, float angle, float scale_x, float scale_y);
void sprite_set_pivot(void *sprite, float pivot_x, float pivot_y);
void sprite_destroy(void *sprite);
void sprite_set_active(void *sprite);
void *sprite_get_active(void);
void sprite_render_pending(void);

// Flash ROM loading
const uint8_t *flash_rom_load(const char *path, size_t *out_size);
void flash_rom_unload(void);

// Keyboard
typedef struct {
    char key;
    bool pressed;
    uint8_t modifiers;
    uint8_t raw_key_code;
} keyboard_event_t;

bool keyboard_read_event(keyboard_event_t *event);
bool keyboard_is_available(void);

// Capabilities
#define HAS_KEYBOARD 1
#define HAS_TOUCH 0
#define HAS_WIFI 0

#ifdef __cplusplus
}
#endif

#endif
