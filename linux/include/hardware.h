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
void display_draw_text(int x, int y, const char *str, uint16_t color, uint16_t bg_color, int font_id, int size);
void display_draw_text_bg(int x, int y, const char *str, uint16_t color, uint16_t bg_color, int font_id, int size, int bg_w, int bg_h);
void display_draw_char_at(int x, int y, unsigned char ch, uint16_t color);
void display_get_jpg_size(const char *path, uint16_t *w, uint16_t *h);
bool display_draw_jpg_fit(const char *path, uint16_t x, uint16_t y);
int display_get_width(void);
int display_get_height(void);
void display_set_backlight(uint8_t brightness);

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
