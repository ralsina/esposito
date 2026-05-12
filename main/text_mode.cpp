#include "text_mode.h"
#include "hardware.h"
#include "lovgfx_config.h"
#include "spleen-5x8.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern "C" {
    // Checkpoint functions are C
    #include "checkpoint.h"
}

static const char *TAG = "text_mode";

// 16-color palette (RGB565 format)
static const uint16_t color_palette[16] = {
    0x0000,  // Black
    0x001F,  // Blue
    0x07E0,  // Green
    0x07FF,  // Cyan
    0xF800,  // Red
    0xF81F,  // Magenta
    0xFFE0,  // Yellow
    0xFFFF,  // White
    0x0841,  // Bright Black (Dark Gray)
    0x001F,  // Bright Blue
    0x07FF,  // Bright Green
    0x07FF,  // Bright Cyan
    0xF800,  // Bright Red
    0xF81F,  // Bright Magenta
    0xFFE0,  // Bright Yellow
    0xFFFF,  // Bright White
};

// Text mode state
typedef struct {
    text_cell_t grid[TEXT_MODE_ROWS][TEXT_MODE_COLS];  // Screen cell grid
    int cursor_x;
    int cursor_y;
    uint8_t bg_color;
    bool initialized;
} text_mode_state_t;

static text_mode_state_t tm_state = {};
static bool graphics_mode = false;

// Screen info
static int get_font_width(void) {
    return TEXT_MODE_CHAR_WIDTH;
}

static int get_font_height(void) {
    return TEXT_MODE_CHAR_HEIGHT;
}

// Convert grid position to pixel position
static void grid_to_pixel(int grid_x, int grid_y, int *pixel_x, int *pixel_y) {
    *pixel_x = grid_x * get_font_width();
    *pixel_y = grid_y * get_font_height();
}

// Update a single cell on the display with attribute support
static void update_cell(int x, int y) {
    if (x < 0 || x >= TEXT_MODE_COLS || y < 0 || y >= TEXT_MODE_ROWS) {
        return;
    }

    text_cell_t *cell = &tm_state.grid[y][x];
    int pixel_x, pixel_y;
    grid_to_pixel(x, y, &pixel_x, &pixel_y);

    // Get base colors
    uint16_t fg_color = color_palette[cell->color & 0x0F];
    uint16_t bg_color = color_palette[tm_state.bg_color & 0x0F];

    // Handle inverse attribute
    if (cell->attributes & TEXT_ATTR_INVERSE) {
        uint16_t temp = fg_color;
        fg_color = bg_color;
        bg_color = temp;
    }

    // Clear cell area with background color
    display_fill_rect(pixel_x, pixel_y, get_font_width(), get_font_height(), bg_color);

    // Draw character if not space
    if (cell->character != ' ') {
        // Handle underline attribute
        if (cell->attributes & TEXT_ATTR_UNDERLINE) {
            // Draw underline (bottom pixel row)
            int underline_y = pixel_y + get_font_height() - 1;
            display_fill_rect(pixel_x, underline_y, get_font_width(), 1, fg_color);
        }

        // Handle bold attribute (simulate by drawing twice with slight offset)
        if (cell->attributes & TEXT_ATTR_BOLD) {
            // Draw character slightly offset for bold effect
            display_draw_text(pixel_x + 1, pixel_y, &cell->character, fg_color);
        }

        // Draw main character
        display_draw_text(pixel_x, pixel_y, &cell->character, fg_color);
    }
}

bool text_mode_init(void) {
    ESP_LOGI(TAG, "Initializing text mode: %dx%d grid", TEXT_MODE_COLS, TEXT_MODE_ROWS);

    // Initialize state
    memset(&tm_state, 0, sizeof(tm_state));
    tm_state.cursor_x = 0;
    tm_state.cursor_y = 0;
    tm_state.bg_color = TEXT_COLOR_BLACK;
    tm_state.initialized = true;
    graphics_mode = false;

    // Initialize grid with spaces
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            tm_state.grid[y][x].character = ' ';
            tm_state.grid[y][x].color = TEXT_COLOR_WHITE;
            tm_state.grid[y][x].attributes = TEXT_ATTR_NORMAL;
        }
    }

    // Clear screen
    display_clear(color_palette[TEXT_COLOR_BLACK]);

    ESP_LOGI(TAG, "Text mode ready with 16 colors and attributes");
    return true;
}

void text_mode_clear(uint16_t bg_color) {
    if (!tm_state.initialized) return;

    // Update background color
    tm_state.bg_color = bg_color;

    // Clear display and reset grid
    display_clear(color_palette[bg_color & 0x0F]);  // Use only lower 4 bits

    // Reset grid to spaces
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            tm_state.grid[y][x].character = ' ';
            tm_state.grid[y][x].color = TEXT_COLOR_WHITE;
            tm_state.grid[y][x].attributes = TEXT_ATTR_NORMAL;
        }
    }
}

void text_mode_print_at(int x, int y, const char *str) {
    text_mode_print_at_color(x, y, str, TEXT_COLOR_WHITE);
}

void text_mode_print_at_color(int x, int y, const char *str, uint16_t color) {
    // Convert old color format to new palette index
    uint8_t palette_color = color & 0x0F;  // Use lower 4 bits
    text_mode_print_at_attr(x, y, str, palette_color, TEXT_ATTR_NORMAL);
}

void text_mode_print_at_attr(int x, int y, const char *str, uint8_t color, uint8_t attributes) {
    if (!tm_state.initialized) return;
    if (x < 0 || x >= TEXT_MODE_COLS || y < 0 || y >= TEXT_MODE_ROWS) return;

    int len = strlen(str);
    int max_chars = TEXT_MODE_COLS - x;

    // Clear the area first for the entire string (batch operation)
    int pixel_x, pixel_y;
    grid_to_pixel(x, y, &pixel_x, &pixel_y);
    int string_width = len * get_font_width();
    display_fill_rect(pixel_x, pixel_y, string_width, get_font_height(), color_palette[tm_state.bg_color & 0x0F]);

    // Print characters until end of line or string
    for (int i = 0; i < len && i < max_chars; i++) {
        tm_state.grid[y][x + i].character = str[i];
        tm_state.grid[y][x + i].color = color;
        tm_state.grid[y][x + i].attributes = attributes;

        // Draw character directly with attributes
        if (str[i] != ' ') {
            update_cell(x + i, y);
        }
    }

    // Update cursor to end of printed text
    tm_state.cursor_x = x + len - 1;
    if (tm_state.cursor_x >= TEXT_MODE_COLS) {
        tm_state.cursor_x = 0;
        tm_state.cursor_y = (tm_state.cursor_y + 1) % TEXT_MODE_ROWS;
    }
}

void text_mode_printf_at(int x, int y, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    text_mode_print_at(x, y, buffer);

    va_end(args);
}

void text_mode_printf_at_color(int x, int y, uint16_t color, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    text_mode_print_at_color(x, y, buffer, color);

    va_end(args);
}

void text_mode_printf_at_attr(int x, int y, uint8_t color, uint8_t attributes, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    text_mode_print_at_attr(x, y, buffer, color, attributes);

    va_end(args);
}

void text_mode_get_cursor(int *x, int *y) {
    if (x) *x = tm_state.cursor_x;
    if (y) *y = tm_state.cursor_y;
}

void text_mode_set_cursor(int x, int y) {
    if (x >= 0 && x < TEXT_MODE_COLS) {
        tm_state.cursor_x = x;
    }
    if (y >= 0 && y < TEXT_MODE_ROWS) {
        tm_state.cursor_y = y;
    }
}

void text_mode_save(void) {
    checkpoint_save_int("tm_cursor_x", tm_state.cursor_x);
    checkpoint_save_int("tm_cursor_y", tm_state.cursor_y);
    checkpoint_save_int("tm_bg_color", tm_state.bg_color);

    // Save grid cells
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            char key[32];
            snprintf(key, sizeof(key), "tm_grid_%d_%d", y, x);
            char char_str[2] = {tm_state.grid[y][x].character, '\0'};
            checkpoint_save_string(key, char_str);

            snprintf(key, sizeof(key), "tm_color_%d_%d", y, x);
            checkpoint_save_int(key, tm_state.grid[y][x].color);

            snprintf(key, sizeof(key), "tm_attr_%d_%d", y, x);
            checkpoint_save_int(key, tm_state.grid[y][x].attributes);
        }
    }

    ESP_LOGI(TAG, "Text mode state saved");
}

void text_mode_restore(void) {
    tm_state.cursor_x = checkpoint_load_int("tm_cursor_x");
    tm_state.cursor_y = checkpoint_load_int("tm_cursor_y");
    tm_state.bg_color = checkpoint_load_int("tm_bg_color");

    // Restore grid and colors
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            char key[32];
            const char *value;

            snprintf(key, sizeof(key), "tm_grid_%d_%d", y, x);
            value = checkpoint_load_string(key);
            if (value != NULL && strlen(value) > 0) {
                tm_state.grid[y][x].character = value[0];
            }

            snprintf(key, sizeof(key), "tm_color_%d_%d", y, x);
            tm_state.grid[y][x].color = checkpoint_load_int(key);

            snprintf(key, sizeof(key), "tm_attr_%d_%d", y, x);
            tm_state.grid[y][x].attributes = checkpoint_load_int(key);
        }
    }

    // Re-render entire screen
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            update_cell(x, y);
        }
    }

    ESP_LOGI(TAG, "Text mode state restored");
}

void text_mode_switch_graphics(void) {
    graphics_mode = true;
    ESP_LOGI(TAG, "Switched to graphics mode (direct LovyanGFX access)");
}

void text_mode_flush(void) {
    // No-op in single buffer mode
}

void text_mode_switch_text(void) {
    graphics_mode = false;

    // Re-render text mode screen
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            update_cell(x, y);
        }
    }

    ESP_LOGI(TAG, "Switched to text mode");
}