#ifndef TEXT_MODE_H
#define TEXT_MODE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Text mode configuration
#define TEXT_MODE_COLS 64      // Number of characters across (320px / 5px per char)
#define TEXT_MODE_ROWS 30      // Number of characters down (240px / 8px per char)
#define TEXT_MODE_CHAR_WIDTH  5
#define TEXT_MODE_CHAR_HEIGHT 8

// Text mode colors (matching TFT colors)
typedef enum {
    TEXT_COLOR_BLACK = 0x0000,
    TEXT_COLOR_WHITE = 0xFFFF,
    TEXT_COLOR_RED = 0xF800,
    TEXT_COLOR_GREEN = 0x07E0,
    TEXT_COLOR_BLUE = 0x001F,
    TEXT_COLOR_YELLOW = 0xFFE0,
    TEXT_COLOR_CYAN = 0x07FF,
    TEXT_COLOR_MAGENTA = 0xF81F,
} text_color_t;

// Initialize text mode
bool text_mode_init(void);

// Clear text mode screen (fills with background color)
void text_mode_clear(uint16_t bg_color);

// Print string at grid position
// x: 0 to TEXT_MODE_COLS-1, y: 0 to TEXT_MODE_ROWS-1
void text_mode_print_at(int x, int y, const char *str);

// Print string at grid position with specific color
void text_mode_print_at_color(int x, int y, const char *str, uint16_t color);

// Print formatted string at grid position
void text_mode_printf_at(int x, int y, const char *fmt, ...);

// Print formatted string at grid position with color
void text_mode_printf_at_color(int x, int y, uint16_t color, const char *fmt, ...);

// Get current cursor position
void text_mode_get_cursor(int *x, int *y);

// Set cursor position
void text_mode_set_cursor(int x, int y);

// Save current text mode state to checkpoint
void text_mode_save(void);

// Restore text mode state from checkpoint
void text_mode_restore(void);

// Switch to graphics mode (direct LovyanGFX access)
void text_mode_switch_graphics(void);

// Switch to text mode
void text_mode_switch_text(void);

// Flush back buffer to display (swap buffers and update only changed cells)
void text_mode_flush(void);

#ifdef __cplusplus
}
#endif

#endif // TEXT_MODE_H