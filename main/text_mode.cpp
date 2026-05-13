#include "text_mode.h"
#include "hardware.h"
#include "lovgfx_config.h"
#include "fonts.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern "C" {
    #include "checkpoint.h"
}

static const char *TAG = "text_mode";

static const uint16_t color_palette[16] = {
    0x0000, 0x0010, 0x0400, 0x0410, 0x8000, 0x8010, 0x8400, 0x8410,
    0x4208, 0x001F, 0x07E0, 0x07FF, 0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

static text_cell_t *grid = NULL;
static int grid_cols = TEXT_MODE_COLS;
static int grid_rows = TEXT_MODE_ROWS;
static int font_width = TEXT_MODE_CHAR_WIDTH;
static int font_height = TEXT_MODE_CHAR_HEIGHT;
static font_id_t current_font = FONT_SPLEEN_5X8;

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t bg_color = 0;
static bool initialized = false;
static bool graphics = false;

int text_mode_get_cols(void) { return grid_cols; }
int text_mode_get_rows(void) { return grid_rows; }
int text_mode_get_char_width(void) { return font_width; }
int text_mode_get_char_height(void) { return font_height; }
font_id_t text_mode_get_font(void) { return current_font; }

static void grid_to_pixel(int gx, int gy, int *px, int *py) {
    *px = gx * font_width;
    *py = gy * font_height;
}

static void update_cell(int x, int y) {
    if (!grid || x < 0 || x >= grid_cols || y < 0 || y >= grid_rows) return;

    text_cell_t *cell = &grid[y * grid_cols + x];
    int px, py;
    grid_to_pixel(x, y, &px, &py);

    uint16_t fg = color_palette[cell->color & 0x0F];
    uint16_t bg = color_palette[cell->bg_color & 0x0F];

    if (cell->attributes & TEXT_ATTR_INVERSE) {
        uint16_t tmp = fg; fg = bg; bg = tmp;
    }

    display_fill_rect(px, py, font_width, font_height, bg);

    if (cell->character != ' ') {
        display_draw_text_bg(px, py, &cell->character, fg, bg);
        if (cell->attributes & TEXT_ATTR_BOLD) {
            display_draw_text_transparent(px + 1, py, &cell->character, fg);
        }
        if (cell->attributes & TEXT_ATTR_UNDERLINE) {
            display_fill_rect(px, py + font_height - 1, font_width, 1, fg);
        }
    }
}

static bool init_grid(font_id_t font) {
    if (grid) {
        free(grid);
        grid = NULL;
    }

    current_font = font;
    font_width = font_table[font].char_width;
    font_height = font_table[font].char_height;
    grid_cols = 320 / font_width;
    grid_rows = 240 / font_height;

    grid = (text_cell_t *)calloc(grid_cols * grid_rows, sizeof(text_cell_t));
    if (!grid) {
        ESP_LOGE(TAG, "Failed to allocate grid: %dx%d", grid_cols, grid_rows);
        grid_cols = TEXT_MODE_COLS;
        grid_rows = TEXT_MODE_ROWS;
        font_width = TEXT_MODE_CHAR_WIDTH;
        font_height = TEXT_MODE_CHAR_HEIGHT;
        current_font = FONT_SPLEEN_5X8;
        return false;
    }

    display_set_font(font_table[font].font_ptr);
    return true;
}

bool text_mode_init_ex(font_id_t font) {
    if ((int)font < 0 || (int)font >= FONT_COUNT) font = FONT_SPLEEN_5X8;

    if (!init_grid(font)) return false;

    cursor_x = 0;
    cursor_y = 0;
    bg_color = TEXT_COLOR_BLACK;
    initialized = true;
    graphics = false;

    for (int y = 0; y < grid_rows; y++) {
        for (int x = 0; x < grid_cols; x++) {
            int idx = y * grid_cols + x;
            grid[idx].character = ' ';
            grid[idx].color = TEXT_COLOR_WHITE;
            grid[idx].bg_color = TEXT_COLOR_BLACK;
            grid[idx].attributes = TEXT_ATTR_NORMAL;
        }
    }

    display_clear(color_palette[TEXT_COLOR_BLACK]);
    ESP_LOGI(TAG, "Text mode: %s (%dx%d grid)", font_table[font].name, grid_cols, grid_rows);
    return true;
}

bool text_mode_init(void) {
    return text_mode_init_ex(FONT_SPLEEN_5X8);
}

void text_mode_clear(uint16_t color_idx) {
    if (!initialized) return;
    bg_color = color_idx;
    display_clear(color_palette[color_idx & 0x0F]);

    for (int y = 0; y < grid_rows; y++) {
        for (int x = 0; x < grid_cols; x++) {
            int idx = y * grid_cols + x;
            grid[idx].character = ' ';
            grid[idx].color = TEXT_COLOR_WHITE;
            grid[idx].bg_color = (uint8_t)color_idx;
            grid[idx].attributes = TEXT_ATTR_NORMAL;
        }
    }
}

void text_mode_print_at(int x, int y, const char *str) {
    text_mode_print_at_color(x, y, str, TEXT_COLOR_WHITE);
}

void text_mode_print_at_color(int x, int y, const char *str, uint16_t color) {
    text_mode_print_at_attr(x, y, str, color & 0x0F, TEXT_ATTR_NORMAL);
}

void text_mode_print_at_attr(int x, int y, const char *str, uint8_t color, uint8_t attr) {
    if (!initialized || !grid) return;
    if (x < 0 || x >= grid_cols || y < 0 || y >= grid_rows) return;

    int len = strlen(str);
    int max_chars = grid_cols - x;

    int px, py;
    grid_to_pixel(x, y, &px, &py);
    int sw = len * font_width;
    display_fill_rect(px, py, sw, font_height, color_palette[bg_color & 0x0F]);

    for (int i = 0; i < len && i < max_chars; i++) {
        int idx = y * grid_cols + x + i;
        grid[idx].character = str[i];
        grid[idx].color = color;
        grid[idx].bg_color = bg_color;
        grid[idx].attributes = attr;

        if (str[i] != ' ') {
            update_cell(x + i, y);
        }
    }

    cursor_x = x + len - 1;
    if (cursor_x >= grid_cols) {
        cursor_x = 0;
        cursor_y = (cursor_y + 1) % grid_rows;
    }
}

void text_mode_print_at_attr_bg(int x, int y, const char *str, uint8_t fg_color, uint8_t bg, uint8_t attr) {
    if (!initialized || !grid) return;
    if (x < 0 || x >= grid_cols || y < 0 || y >= grid_rows) return;

    int len = strlen(str);
    int max_chars = grid_cols - x;

    int px, py;
    grid_to_pixel(x, y, &px, &py);
    int sw = len * font_width;
    display_fill_rect(px, py, sw, font_height, color_palette[bg & 0x0F]);

    for (int i = 0; i < len && i < max_chars; i++) {
        int idx = y * grid_cols + x + i;
        grid[idx].character = str[i];
        grid[idx].color = fg_color;
        grid[idx].bg_color = bg;
        grid[idx].attributes = attr;

        if (str[i] != ' ') {
            update_cell(x + i, y);
        }
    }

    cursor_x = x + len - 1;
    if (cursor_x >= grid_cols) {
        cursor_x = 0;
        cursor_y = (cursor_y + 1) % grid_rows;
    }
}

void text_mode_printf_at(int x, int y, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    text_mode_print_at(x, y, buf);
    va_end(args);
}

void text_mode_printf_at_color(int x, int y, uint16_t color, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    text_mode_print_at_color(x, y, buf, color);
    va_end(args);
}

void text_mode_printf_at_attr(int x, int y, uint8_t color, uint8_t attr, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    text_mode_print_at_attr(x, y, buf, color, attr);
    va_end(args);
}

void text_mode_printf_at_attr_bg(int x, int y, uint8_t fg_color, uint8_t bg, uint8_t attr, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    text_mode_print_at_attr_bg(x, y, buf, fg_color, bg, attr);
    va_end(args);
}

void text_mode_get_cursor(int *x, int *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void text_mode_set_cursor(int x, int y) {
    if (x >= 0 && x < grid_cols) cursor_x = x;
    if (y >= 0 && y < grid_rows) cursor_y = y;
}

void text_mode_save(void) {
    checkpoint_save_int("tm_cursor_x", cursor_x);
    checkpoint_save_int("tm_cursor_y", cursor_y);
    checkpoint_save_int("tm_bg_color", bg_color);
    checkpoint_save_int("tm_font", current_font);

    for (int y = 0; y < grid_rows; y++) {
        char key[32];
        char chars[256], attrs[1280];
        int cp = 0, ap = 0;

        for (int x = 0; x < grid_cols; x++) {
            text_cell_t *cell = &grid[y * grid_cols + x];
            if (cp < (int)sizeof(chars) - 1) {
                chars[cp++] = cell->character ? cell->character : ' ';
            }
            if (ap < (int)sizeof(attrs) - 10) {
                ap += snprintf(attrs + ap, sizeof(attrs) - ap,
                    "%d,%d,%d;", cell->color, cell->bg_color, cell->attributes);
            }
        }
        chars[cp] = '\0';
        attrs[ap] = '\0';

        snprintf(key, sizeof(key), "tm_rc_%d", y);
        checkpoint_save_string(key, chars);
        snprintf(key, sizeof(key), "tm_ra_%d", y);
        checkpoint_save_string(key, attrs);
    }
}

void text_mode_restore(void) {
    cursor_x = checkpoint_load_int("tm_cursor_x");
    cursor_y = checkpoint_load_int("tm_cursor_y");
    bg_color = checkpoint_load_int("tm_bg_color");
    font_id_t saved_font = (font_id_t)checkpoint_load_int("tm_font");
    if ((int)saved_font < 0 || (int)saved_font >= FONT_COUNT) saved_font = FONT_SPLEEN_5X8;

    init_grid(saved_font);

    for (int y = 0; y < grid_rows; y++) {
        char key[32];
        snprintf(key, sizeof(key), "tm_rc_%d", y);
        const char *chars = checkpoint_load_string(key);
        snprintf(key, sizeof(key), "tm_ra_%d", y);
        const char *attrs = checkpoint_load_string(key);

        int x = 0;
        if (chars) {
            for (; x < grid_cols && chars[x]; x++) {
                grid[y * grid_cols + x].character = chars[x];
            }
        }
        if (attrs) {
            const char *p = attrs;
            x = 0;
            while (*p && x < grid_cols) {
                int c = 0, b = 0, a = 0;
                int n = sscanf(p, "%d,%d,%d;", &c, &b, &a);
                if (n >= 3) {
                    grid[y * grid_cols + x].color = (uint8_t)c;
                    grid[y * grid_cols + x].bg_color = (uint8_t)b;
                    grid[y * grid_cols + x].attributes = (uint8_t)a;
                } else if (n >= 2) {
                    grid[y * grid_cols + x].color = (uint8_t)c;
                    grid[y * grid_cols + x].attributes = (uint8_t)a;
                }
                while (*p && *p != ';') p++;
                if (*p == ';') p++;
                x++;
            }
        }
    }

    for (int y = 0; y < grid_rows; y++) {
        for (int x = 0; x < grid_cols; x++) {
            update_cell(x, y);
        }
    }
}

void text_mode_switch_graphics(void) {
    graphics = true;
}

void text_mode_flush(void) {}

void text_mode_switch_text(void) {
    graphics = false;
    for (int y = 0; y < grid_rows; y++) {
        for (int x = 0; x < grid_cols; x++) {
            update_cell(x, y);
        }
    }
}
