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

// Text mode state
typedef struct {
    char grid[TEXT_MODE_ROWS][TEXT_MODE_COLS];  // Screen character grid
    uint16_t colors[TEXT_MODE_ROWS][TEXT_MODE_COLS]; // Color for each cell
    int cursor_x;
    int cursor_y;
    uint16_t bg_color;
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

// Update a single cell on the display
static void update_cell(int x, int y) {
    if (x < 0 || x >= TEXT_MODE_COLS || y < 0 || y >= TEXT_MODE_ROWS) {
        return;
    }

    int pixel_x, pixel_y;
    grid_to_pixel(x, y, &pixel_x, &pixel_y);

    // Clear cell area
    display_fill_rect(pixel_x, pixel_y, get_font_width(), get_font_height(), tm_state.bg_color);

    // Draw character if not space
    if (tm_state.grid[y][x] != ' ') {
        display_draw_text(pixel_x, pixel_y, &tm_state.grid[y][x], tm_state.colors[y][x]);
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
            tm_state.grid[y][x] = ' ';
            tm_state.colors[y][x] = TEXT_COLOR_WHITE;
        }
    }

    // Clear screen
    display_clear(TEXT_COLOR_BLACK);

    ESP_LOGI(TAG, "Text mode ready");
    return true;
}

void text_mode_clear(uint16_t bg_color) {
    if (!tm_state.initialized) return;

    // Update background color
    tm_state.bg_color = bg_color;

    // Clear display and reset grid
    display_clear(bg_color);

    // Reset grid to spaces and clear colors
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            tm_state.grid[y][x] = ' ';
            tm_state.colors[y][x] = TEXT_COLOR_WHITE;
        }
    }
}

void text_mode_print_at(int x, int y, const char *str) {
    text_mode_print_at_color(x, y, str, TEXT_COLOR_WHITE);
}

void text_mode_print_at_color(int x, int y, const char *str, uint16_t color) {
    if (!tm_state.initialized) return;
    if (x < 0 || x >= TEXT_MODE_COLS || y < 0 || y >= TEXT_MODE_ROWS) return;

    int len = strlen(str);
    int max_chars = TEXT_MODE_COLS - x;

    // Clear the area first for the entire string (batch operation)
    int pixel_x, pixel_y;
    grid_to_pixel(x, y, &pixel_x, &pixel_y);
    int string_width = len * get_font_width();
    display_fill_rect(pixel_x, pixel_y, string_width, get_font_height(), tm_state.bg_color);

    // Print characters until end of line or string
    for (int i = 0; i < len && i < max_chars; i++) {
        tm_state.grid[y][x + i] = str[i];
        tm_state.colors[y][x + i] = color;

        // Draw character directly without clearing again
        if (str[i] != ' ') {
            int char_x = pixel_x + (i * get_font_width());
            display_draw_text(char_x, pixel_y, &str[i], color);
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

    // Save grid and colors
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        for (int x = 0; x < TEXT_MODE_COLS; x++) {
            char key[32];
            snprintf(key, sizeof(key), "tm_grid_%d_%d", y, x);
            checkpoint_save_string(key, &tm_state.grid[y][x]);

            snprintf(key, sizeof(key), "tm_color_%d_%d", y, x);
            checkpoint_save_int(key, tm_state.colors[y][x]);
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
                tm_state.grid[y][x] = value[0];
            }

            snprintf(key, sizeof(key), "tm_color_%d_%d", y, x);
            tm_state.colors[y][x] = checkpoint_load_int(key);
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