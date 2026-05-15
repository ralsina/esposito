#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdbool.h>
#include "os_core.h"

// Forward declaration for LovyanGFX display (C++ compatible)
#ifdef __cplusplus
class LGFX;
extern LGFX* display_tft;
#else
typedef void LGFX;  // Opaque pointer for C
extern LGFX* display_tft;
#endif

// Hardware initialization
#ifdef __cplusplus
extern "C" {
#endif

bool hardware_init(void);

// Display functions
bool display_init(void);
void display_clear(uint16_t color);
void display_draw_text(int x, int y, const char *text, uint16_t color);
void display_draw_text_transparent(int x, int y, const char *text, uint16_t color);
void display_draw_text_bg(int x, int y, const char *text, uint16_t fg, uint16_t bg);
void display_draw_pixel(int x, int y, uint16_t color);
void display_fill_rect(int x, int y, int width, int height, uint16_t color);
void display_set_font(const void *font);
void display_measure_scaled_text(const char *text, int scale, int *width, int *height);
void display_draw_scaled_text_bg(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
bool display_get_jpg_size(const char *path, int *width, int *height);
bool display_draw_jpg_fit(const char *path, int *drawn_width, int *drawn_height);

// Display character with specific foreground and background at pixel position
void display_draw_char_at(int x, int y, char ch, uint16_t fg_color, uint16_t bg_color);
bool display_save_screenshot_ppm(const char *path);

// Keyboard functions
bool keyboard_init(void);
void keyboard_deinit(void);
bool keyboard_read_event(event_t *event);
bool keyboard_is_available(void);

// Timer functions
bool timer_init(void);
void timer_set_interval(uint32_t interval_ms);

// Serial functions
bool serial_init(int baud, int data_bits, char parity, int stop_bits);
void serial_deinit(void);
size_t serial_read(char *buffer, size_t max_len);
size_t serial_write(const char *data, size_t len);
void serial_log_output_set_enabled(bool enabled);
bool serial_log_output_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_H
