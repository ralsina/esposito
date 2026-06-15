#ifndef TEXT_MODE_H
#define TEXT_MODE_H

#include <stdint.h>
#include <stdbool.h>

#define TEXT_MODE_COLS 64
#define TEXT_MODE_ROWS 30
#define TEXT_MODE_CHAR_WIDTH  8
#define TEXT_MODE_CHAR_HEIGHT 16

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

typedef enum {
    TEXT_ATTR_NORMAL = 0,
    TEXT_ATTR_BOLD = 1,
    TEXT_ATTR_ITALIC = 2,
    TEXT_ATTR_UNDERLINE = 4,
    TEXT_ATTR_INVERSE = 8,
    TEXT_ATTR_SYMBOL = 16,
    TEXT_ATTR_LINE_DRAWING = 16,
    TEXT_ATTR_BORDER_TOP = 32,
    TEXT_ATTR_BORDER_LEFT = 64,
    TEXT_ATTR_BORDER_RIGHT = 128,
} text_attribute_t;

typedef struct {
    uint16_t character;
    uint8_t color;
    uint8_t bg_color;
    uint8_t attributes;
} text_cell_t;

#ifdef __cplusplus
extern "C" {
#endif

bool text_mode_init(void);
void text_mode_clear(uint16_t bg_color);
int text_mode_get_cols(void);
int text_mode_get_rows(void);
int text_mode_get_char_width(void);
int text_mode_get_char_height(void);
void text_mode_set_cursor(int x, int y);
void text_mode_get_cursor(int *x, int *y);

void text_mode_print_at(int x, int y, const char *str);
void text_mode_print_at_color(int x, int y, const char *str, uint16_t color);
void text_mode_printf_at(int x, int y, const char *fmt, ...);
void text_mode_printf_at_color(int x, int y, uint16_t color, const char *fmt, ...);
void text_mode_print_at_attr(int x, int y, const char *str, uint8_t color, uint8_t attr);
void text_mode_printf_at_attr(int x, int y, uint8_t color, uint8_t attr, const char *fmt, ...);
void text_mode_print_at_attr_bg(int x, int y, const char *str, uint8_t fg, uint8_t bg, uint8_t attr);
void text_mode_printf_at_attr_bg(int x, int y, uint8_t fg, uint8_t bg, uint8_t attr, const char *fmt, ...);
void text_mode_flush(void);

// Snapshots (stubs)
typedef struct { text_cell_t *cells; int cols, rows; int cursor_x, cursor_y; } text_mode_snapshot_t;
text_mode_snapshot_t *text_mode_save_snapshot(void);
void text_mode_restore_snapshot(text_mode_snapshot_t *snap);
void text_mode_free_snapshot(text_mode_snapshot_t *snap);

// Pixel coordinate helpers (stubs)
void text_mode_pixel_to_cell(int px, int py, int *cx, int *cy);
void text_mode_cell_to_pixel(int cx, int cy, int *px, int *py);

#ifdef __cplusplus
}
#endif

#endif
