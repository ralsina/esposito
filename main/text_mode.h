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
    TEXT_ATTR_SYMBOL = 16,          // Render from symbol font
    TEXT_ATTR_LINE_DRAWING = 16,    // Alias for backward compat
    TEXT_ATTR_BORDER_TOP = 32,
    TEXT_ATTR_BORDER_LEFT = 64,
    TEXT_ATTR_BORDER_RIGHT = 128,
} text_attribute_t;

// Symbol codepoints for use with TEXT_ATTR_SYMBOL
// These are Unicode codepoints stored in text_cell_t.character (uint16_t)
#define SYM_LEFT      0x2190    // ←
#define SYM_UP        0x2191    // ↑
#define SYM_RIGHT     0x2192    // →
#define SYM_DOWN      0x2193    // ↓
#define SYM_LEFTRIGHT 0x2194   // ↔
#define SYM_UPDOWN    0x2195   // ↕
#define SYM_CHECK     0x2713    // ✓
#define SYM_CROSS     0x2717    // ✗
#define SYM_MENU      0x2261    // ≡
#define SYM_GEAR      0x2699    // ⚙
#define SYM_RETURN    0x23CE    // ⏎
#define SYM_BOX       0x2610    // ☐
#define SYM_CHECKBOX  0x2611    // ☑
#define SYM_ELLIPSIS  0x2026    // …
#define SYM_LTE       0x2264    // ≤
#define SYM_GTE       0x2265    // ≥
#define SYM_NEQ       0x2260    // ≠
#define SYM_BULLET    0x25CF    // ●
#define SYM_CIRCLE    0x25CB    // ○
#define SYM_SQUARE    0x25A0    // ■
#define SYM_SQUARE_O  0x25A1    // □
#define SYM_DIAMOND   0x25C6    // ◆
#define SYM_DIAMOND_O 0x25C7    // ◇
#define SYM_TRI_UP    0x25B2    // ▲
#define SYM_TRI_DOWN  0x25BC    // ▼
#define SYM_TRI_LEFT  0x25C0    // ◀
#define SYM_TRI_RIGHT 0x25B6    // ▶
#define SYM_SEARCH    0x2315    // ⌕

// Cell data structure
typedef struct {
    uint16_t character;  // Unicode codepoint
    uint8_t color;
    uint8_t bg_color;
    uint8_t attributes;
} text_cell_t;

// Initialize text mode with default font (spleen-5x8)
bool text_mode_init(void);

// Initialize text mode with a specific font
bool text_mode_init_ex(font_id_t font);

// Change font at runtime (requires text mode already initialized)
bool text_mode_set_font(font_id_t font);

// Apply user's configured font from settings (call after settings system is ready)
bool text_mode_apply_configured_font(void);

// Recalculate grid for rotation changes (preserves current font)
void text_mode_reinit_grid(void);

// Runtime grid queries (use these instead of TEXT_MODE_COLS/ROWS for font-aware code)
int text_mode_get_cols(void);
int text_mode_get_rows(void);
int text_mode_get_char_width(void);
int text_mode_get_char_height(void);
font_id_t text_mode_get_font(void);
font_variant_t text_mode_get_variant(void);

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

// Switch to graphics mode (direct LovyanGFX access)
void text_mode_switch_graphics(void);

// Switch to text mode
void text_mode_switch_text(void);

// Flush back buffer to display (swap buffers and update only changed cells)
void text_mode_flush(void);

// Save screenshot of current text grid as PPM on SD card
bool text_mode_save_screenshot(void);

// Screen snapshot for save/restore operations
typedef struct {
    text_cell_t *cells;  // Copy of entire text grid
    int cols;            // Grid dimensions when saved
    int rows;
    int cursor_x;        // Cursor position when saved
    int cursor_y;
    uint8_t bg_color;    // Background color when saved
    font_id_t font;      // Font in use when saved
    font_variant_t variant; // Font variant when saved
} text_mode_snapshot_t;

// Save current screen state to a snapshot
text_mode_snapshot_t* text_mode_save_snapshot(void);

// Restore screen state from a snapshot
void text_mode_restore_snapshot(text_mode_snapshot_t *snapshot);

// Free a snapshot and its associated memory
void text_mode_free_snapshot(text_mode_snapshot_t *snapshot);

// Convert pixel coordinates to character grid coordinates
void text_mode_pixel_to_cell(int pixel_x, int pixel_y, int *cell_x, int *cell_y);

// Convert character grid coordinates to pixel coordinates
void text_mode_cell_to_pixel(int cell_x, int cell_y, int *pixel_x, int *pixel_y);

#ifdef __cplusplus
}
#endif

#endif // TEXT_MODE_H