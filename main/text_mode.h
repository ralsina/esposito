#ifndef TEXT_MODE_H
#define TEXT_MODE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default grid constants (for default spleen-5x8 font)
#define TEXT_MODE_COLS 64
#define TEXT_MODE_ROWS 30
#define TEXT_MODE_CHAR_WIDTH  5
#define TEXT_MODE_CHAR_HEIGHT 8

// Text mode colors (16 color palette - CGA/EGA style)
typedef enum {
    TEXT_COLOR_BLACK = 0,
    TEXT_COLOR_BLUE = 1,
    TEXT_COLOR_GREEN = 2,
    TEXT_COLOR_CYAN = 3,
    TEXT_COLOR_RED = 4,
    TEXT_COLOR_MAGENTA = 5,
    TEXT_COLOR_YELLOW = 6,
    TEXT_COLOR_WHITE = 7,
    TEXT_COLOR_BRIGHT_BLACK = 8,
    TEXT_COLOR_BRIGHT_BLUE = 9,
    TEXT_COLOR_BRIGHT_GREEN = 10,
    TEXT_COLOR_BRIGHT_CYAN = 11,
    TEXT_COLOR_BRIGHT_RED = 12,
    TEXT_COLOR_BRIGHT_MAGENTA = 13,
    TEXT_COLOR_BRIGHT_YELLOW = 14,
    TEXT_COLOR_BRIGHT_WHITE = 15,
} text_color_t;

// Text attributes
typedef enum {
    TEXT_ATTR_NORMAL = 0,
    TEXT_ATTR_BOLD = 1,
    TEXT_ATTR_ITALIC = 2,
    TEXT_ATTR_UNDERLINE = 4,
    TEXT_ATTR_INVERSE = 8,
} text_attribute_t;

// Cell data structure
typedef struct {
    char character;
    uint8_t color;      // foreground color (0-15)
    uint8_t bg_color;   // background color (0-15)
    uint8_t attributes;
} text_cell_t;

// Initialize text mode with default font (spleen-5x8)
bool text_mode_init(void);

// Initialize text mode with a specific font
bool text_mode_init_ex(font_id_t font);

// Runtime grid queries (use these instead of TEXT_MODE_COLS/ROWS for font-aware code)
int text_mode_get_cols(void);
int text_mode_get_rows(void);
int text_mode_get_char_width(void);
int text_mode_get_char_height(void);
font_id_t text_mode_get_font(void);

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

// Print with attributes
void text_mode_print_at_attr(int x, int y, const char *str, uint8_t color, uint8_t attributes);
void text_mode_printf_at_attr(int x, int y, uint8_t color, uint8_t attributes, const char *fmt, ...);

// Print with foreground color, background color, and attributes
void text_mode_print_at_attr_bg(int x, int y, const char *str, uint8_t fg_color, uint8_t bg_color, uint8_t attributes);
void text_mode_printf_at_attr_bg(int x, int y, uint8_t fg_color, uint8_t bg_color, uint8_t attributes, const char *fmt, ...);

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

// Save screenshot of current text grid as PPM on SD card
bool text_mode_save_screenshot(void);

#ifdef __cplusplus
}
#endif

#endif // TEXT_MODE_H