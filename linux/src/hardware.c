#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Display stubs — apps use text_mode for rendering
void display_clear(uint16_t color) { (void)color; }
void display_draw_pixel(uint16_t x, uint16_t y, uint16_t color) { (void)x;(void)y;(void)color; }
void display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) { (void)x;(void)y;(void)w;(void)h;(void)color; }
void display_draw_text(int x, int y, const char *str, uint16_t color, uint16_t bg_color, int font_id, int size) { (void)x;(void)y;(void)str;(void)color;(void)bg_color;(void)font_id;(void)size; }
void display_draw_text_bg(int x, int y, const char *str, uint16_t color, uint16_t bg_color, int font_id, int size, int bg_w, int bg_h) { (void)x;(void)y;(void)str;(void)color;(void)bg_color;(void)font_id;(void)size;(void)bg_w;(void)bg_h; }
void display_draw_char_at(int x, int y, unsigned char ch, uint16_t color) { (void)x;(void)y;(void)ch;(void)color; }
void display_get_jpg_size(const char *path, uint16_t *w, uint16_t *h) { (void)path; if(w)*w=0; if(h)*h=0; }
bool display_draw_jpg_fit(const char *path, uint16_t x, uint16_t y) { (void)path;(void)x;(void)y; return false; }
int display_get_width(void) { return 320; }
int display_get_height(void) { return 240; }
void display_set_backlight(uint8_t brightness) { (void)brightness; }

// Keyboard — real implementation in main.c's event loop, this catches non-SDL usage
bool keyboard_read_event(keyboard_event_t *event) { (void)event; return false; }
bool keyboard_is_available(void) { return true; }
