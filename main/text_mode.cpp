#include "text_mode.h"
#include "hardware.h"
#include "fonts.h"
#include "sd_card.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

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
static font_id_t current_font = FONT_HACK_8;
static font_variant_t current_variant = FONT_VARIANT_REGULAR;

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t bg_color = 0;
static bool initialized = false;
static bool graphics = false;

static void update_cell(int x, int y);

enum line_drawing_mask_t {
    LINE_DRAW_LEFT = 1 << 0,
    LINE_DRAW_RIGHT = 1 << 1,
    LINE_DRAW_UP = 1 << 2,
    LINE_DRAW_DOWN = 1 << 3,
};

static bool cell_uses_line_drawing(const text_cell_t *cell) {
    return cell && (cell->attributes & TEXT_ATTR_LINE_DRAWING);
}

static uint8_t line_drawing_base_mask(char ch) {
    switch (ch) {
        case '-': return LINE_DRAW_LEFT | LINE_DRAW_RIGHT;
        case 'q': return LINE_DRAW_LEFT | LINE_DRAW_RIGHT;
        case '|': return LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 'x': return LINE_DRAW_UP | LINE_DRAW_DOWN;
        case '+': return LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 'n': return LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 'l': return LINE_DRAW_RIGHT | LINE_DRAW_DOWN;
        case 'k': return LINE_DRAW_LEFT | LINE_DRAW_DOWN;
        case 'm': return LINE_DRAW_RIGHT | LINE_DRAW_UP;
        case 'j': return LINE_DRAW_LEFT | LINE_DRAW_UP;
        case 't': return LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 'u': return LINE_DRAW_LEFT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 'v': return LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP;
        case 'w': return LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_DOWN;
        default: return 0;
    }
}

static bool neighbor_connects_to_direction(int nx, int ny, uint8_t opposite_mask) {
    if (!grid || nx < 0 || ny < 0 || nx >= grid_cols || ny >= grid_rows) {
        return false;
    }

    const text_cell_t *neighbor = &grid[ny * grid_cols + nx];
    if (!cell_uses_line_drawing(neighbor)) {
        return false;
    }

    return (line_drawing_base_mask(neighbor->character) & opposite_mask) != 0;
}

static uint8_t line_drawing_mask_for_cell(int x, int y) {
    if (!grid || x < 0 || y < 0 || x >= grid_cols || y >= grid_rows) {
        return 0;
    }

    const text_cell_t *cell = &grid[y * grid_cols + x];
    if (!cell_uses_line_drawing(cell)) {
        return 0;
    }

    uint8_t mask = line_drawing_base_mask(cell->character);
    if (cell->character == '+') {
        mask = 0;
        if (neighbor_connects_to_direction(x - 1, y, LINE_DRAW_RIGHT)) {
            mask |= LINE_DRAW_LEFT;
        }
        if (neighbor_connects_to_direction(x + 1, y, LINE_DRAW_LEFT)) {
            mask |= LINE_DRAW_RIGHT;
        }
        if (neighbor_connects_to_direction(x, y - 1, LINE_DRAW_DOWN)) {
            mask |= LINE_DRAW_UP;
        }
        if (neighbor_connects_to_direction(x, y + 1, LINE_DRAW_UP)) {
            mask |= LINE_DRAW_DOWN;
        }

        if (mask == 0) {
            mask = LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        }
    }

    return mask;
}

static void draw_line_drawing_cell(int px, int py, uint8_t fg, uint8_t mask) {
    int mid_x = px + font_width / 2;
    int mid_y = py + font_height / 2;
    int right = px + font_width - 1;
    int bottom = py + font_height - 1;

    if ((mask & LINE_DRAW_LEFT) && (mask & LINE_DRAW_RIGHT)) {
        display_fill_rect(px, mid_y, font_width, 1, fg);
    } else if (mask & LINE_DRAW_LEFT) {
        display_fill_rect(px, mid_y, mid_x - px + 1, 1, fg);
    } else if (mask & LINE_DRAW_RIGHT) {
        display_fill_rect(mid_x, mid_y, right - mid_x + 1, 1, fg);
    }

    if ((mask & LINE_DRAW_UP) && (mask & LINE_DRAW_DOWN)) {
        display_fill_rect(mid_x, py, 1, font_height, fg);
    } else if (mask & LINE_DRAW_UP) {
        display_fill_rect(mid_x, py, 1, mid_y - py + 1, fg);
    } else if (mask & LINE_DRAW_DOWN) {
        display_fill_rect(mid_x, mid_y, 1, bottom - mid_y + 1, fg);
    }
}

static void refresh_line_drawing_cells_around(int x, int y) {
    static const int offsets[][2] = {
        {0, 0},
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    };

    for (size_t index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
        int nx = x + offsets[index][0];
        int ny = y + offsets[index][1];
        if (!grid || nx < 0 || ny < 0 || nx >= grid_cols || ny >= grid_rows) {
            continue;
        }

        text_cell_t *cell = &grid[ny * grid_cols + nx];
        if (cell->attributes & TEXT_ATTR_LINE_DRAWING) {
            update_cell(nx, ny);
        }
    }
}

int text_mode_get_cols(void) { return grid_cols; }
int text_mode_get_rows(void) { return grid_rows; }
int text_mode_get_char_width(void) { return font_width; }
int text_mode_get_char_height(void) { return font_height; }
font_id_t text_mode_get_font(void) { return current_font; }
font_variant_t text_mode_get_variant(void) { return current_variant; }

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

    if (cell->attributes & TEXT_ATTR_LINE_DRAWING) {
        uint8_t mask = line_drawing_mask_for_cell(x, y);
        if (mask != 0 && cell->character != ' ') {
            draw_line_drawing_cell(px, py, fg, mask);
        }
        return;
    }

    if (cell->character != ' ') {
        font_variant_t needed = FONT_VARIANT_REGULAR;
        if (cell->attributes & TEXT_ATTR_BOLD && cell->attributes & TEXT_ATTR_ITALIC) {
            needed = FONT_VARIANT_BOLDITALIC;
        } else if (cell->attributes & TEXT_ATTR_BOLD) {
            needed = FONT_VARIANT_BOLD;
        } else if (cell->attributes & TEXT_ATTR_ITALIC) {
            needed = FONT_VARIANT_ITALIC;
        }
        if (needed != current_variant) {
            if (display_load_font(current_font, needed)) {
                current_variant = needed;
            }
        }
        display_draw_char_at(px, py, cell->character, fg, bg);
    }

    // Draw borders
    if (cell->attributes & TEXT_ATTR_UNDERLINE) {
        display_fill_rect(px, py + font_height - 1, font_width, 1, fg);
    }
    if (cell->attributes & TEXT_ATTR_BORDER_TOP) {
        display_fill_rect(px, py, font_width, 1, fg);
    }
    if (cell->attributes & TEXT_ATTR_BORDER_LEFT) {
        display_fill_rect(px, py, 1, font_height, fg);
    }
    if (cell->attributes & TEXT_ATTR_BORDER_RIGHT) {
        display_fill_rect(px + font_width - 1, py, 1, font_height, fg);
    }
}

static bool init_grid(font_id_t font) {
    const int display_width = display_get_width();
    const int display_height = display_get_height();

    const int max_cols = display_width / 4;
    const int max_rows = display_height / 6;
    const int max_cells = max_cols * max_rows;

    if (grid) {
        free(grid);
        grid = NULL;
    }

    if (font < 0 || font >= FONT_COUNT) font = FONT_HACK_8;
    current_font = font;
    current_variant = FONT_VARIANT_REGULAR;
    font_width = font_table[font].char_width;
    font_height = font_table[font].char_height;

    grid_cols = display_width / font_width;
    grid_rows = display_height / font_height;

    grid = (text_cell_t *)calloc(max_cells, sizeof(text_cell_t));
    if (!grid) {
        ESP_LOGE(TAG, "Failed to allocate grid: %dx%d", max_cols, max_rows);
        grid_cols = TEXT_MODE_COLS;
        grid_rows = TEXT_MODE_ROWS;
        font_width = TEXT_MODE_CHAR_WIDTH;
        font_height = TEXT_MODE_CHAR_HEIGHT;
        current_font = FONT_HACK_8;
        current_variant = FONT_VARIANT_REGULAR;
        return false;
    }

    display_load_font(font, FONT_VARIANT_REGULAR);

    ESP_LOGI(TAG, "Grid allocated: %dx%d (max %dx%d), font: %s (%dx%d)",
             grid_cols, grid_rows, max_cols, max_rows,
             font_table[font].name, font_width, font_height);
    return true;
}

bool text_mode_init_ex(font_id_t font) {
    if (font < 0 || font >= FONT_COUNT) font = FONT_HACK_8;

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
    char font_setting[32];
    extern size_t os_settings_get_string(const char *key, const char *default_val, char *out, size_t out_size);
    extern font_id_t font_lookup_by_name(const char *name);

    size_t len = os_settings_get_string("system/default_font", "hack 8", font_setting, sizeof(font_setting));

    font_id_t default_font = font_lookup_by_name(font_setting);
    if (default_font < 0 || default_font >= FONT_COUNT) {
        default_font = FONT_HACK_8;
    }

    ESP_LOGI(TAG, "text_mode_init: using font %s (read '%s' from settings, len=%d)",
             font_table[default_font].name, font_setting, (int)len);

    return text_mode_init_ex(default_font);
}

bool text_mode_apply_configured_font(void) {
    if (!initialized) {
        ESP_LOGE(TAG, "Cannot apply configured font: text mode not initialized");
        return false;
    }

    char font_setting[32];
    extern size_t os_settings_get_string(const char *key, const char *default_val, char *out, size_t out_size);
    extern font_id_t font_lookup_by_name(const char *name);

    size_t len = os_settings_get_string("system/default_font", "hack 8", font_setting, sizeof(font_setting));
    ESP_LOGI(TAG, "Font setting read: len=%d, value='%s'", (int)len, font_setting);

    font_id_t configured_font = font_lookup_by_name(font_setting);
    ESP_LOGI(TAG, "Font lookup result: %d (FONT_COUNT=%d)", (int)configured_font, FONT_COUNT);

    if (configured_font < 0 || configured_font >= FONT_COUNT) {
        ESP_LOGW(TAG, "Invalid font ID %d, falling back to hack 8", (int)configured_font);
        configured_font = FONT_HACK_8;
    }

    ESP_LOGI(TAG, "Applying configured font: %s (%s, ID=%d)",
             font_setting, font_table[configured_font].name, (int)configured_font);

    return text_mode_set_font(configured_font);
}

bool text_mode_set_font(font_id_t font) {
    if (!initialized) {
        ESP_LOGE(TAG, "Cannot set font: text mode not initialized");
        return false;
    }

    if (font < 0 || font >= FONT_COUNT) {
        ESP_LOGE(TAG, "Invalid font ID: %d", (int)font);
        return false;
    }

    if (font == current_font) {
        return true;
    }

    ESP_LOGI(TAG, "Changing font from %s to %s",
             font_table[current_font].name, font_table[font].name);

    current_font = font;
    current_variant = FONT_VARIANT_REGULAR;
    font_width = font_table[font].char_width;
    font_height = font_table[font].char_height;

    const int display_width = display_get_width();
    const int display_height = display_get_height();
    int new_cols = display_width / font_width;
    int new_rows = display_height / font_height;

    ESP_LOGI(TAG, "Grid dimensions: %dx%d -> %dx%d", grid_cols, grid_rows, new_cols, new_rows);

    grid_cols = new_cols;
    grid_rows = new_rows;

    for (int y = 0; y < grid_rows; y++) {
        for (int x = 0; x < grid_cols; x++) {
            int idx = y * 80 + x;
            if (idx < 3200) {
                grid[idx].character = ' ';
                grid[idx].color = TEXT_COLOR_WHITE;
                grid[idx].bg_color = TEXT_COLOR_BLACK;
                grid[idx].attributes = TEXT_ATTR_NORMAL;
            }
        }
    }

    cursor_x = 0;
    cursor_y = 0;

    display_load_font(font, FONT_VARIANT_REGULAR);
    display_clear(color_palette[TEXT_COLOR_BLACK]);

    ESP_LOGI(TAG, "Font changed to %s (%dx%d grid)",
             font_table[font].name, grid_cols, grid_rows);
    return true;
}

void text_mode_reinit_grid(void) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot reinit grid: text mode not initialized");
        return;
    }

    // Recalculate grid dimensions based on current display dimensions (respects rotation)
    const int display_width = display_get_width();
    const int display_height = display_get_height();

    ESP_LOGI(TAG, "text_mode_reinit_grid: display dims %dx%d, font %dx%d",
             display_width, display_height, font_width, font_height);

    int new_cols = display_width / font_width;
    int new_rows = display_height / font_height;

    ESP_LOGI(TAG, "Grid reinit for rotation: %dx%d -> %dx%d", grid_cols, grid_rows, new_cols, new_rows);

    // Update grid dimensions
    grid_cols = new_cols;
    grid_rows = new_rows;

    // Clear and reset grid
    for (int y = 0; y < grid_rows; y++) {
        for (int x = 0; x < grid_cols; x++) {
            int idx = y * 80 + x;  // Use max columns for indexing
            if (idx < 3200) {  // Safety check
                grid[idx].character = ' ';
                grid[idx].color = TEXT_COLOR_WHITE;
                grid[idx].bg_color = TEXT_COLOR_BLACK;
                grid[idx].attributes = TEXT_ATTR_NORMAL;
            }
        }
    }

    // Reset cursor position
    cursor_x = 0;
    cursor_y = 0;

    // Clear the screen
    display_clear(color_palette[TEXT_COLOR_BLACK]);

    ESP_LOGI(TAG, "Grid reinitialized to %dx%d for rotation", grid_cols, grid_rows);
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

static void text_mode_write_cells(int x, int y, const char *str, uint8_t fg_color, uint8_t bg, uint8_t attr) {
    if (!initialized || !grid || !str) return;
    if (x < 0 || x >= grid_cols || y < 0 || y >= grid_rows) return;

    int len = strlen(str);
    int max_chars = grid_cols - x;
    int write_len = len < max_chars ? len : max_chars;

    for (int i = 0; i < write_len; i++) {
        int idx = y * grid_cols + x + i;
        text_cell_t *cell = &grid[idx];

        if (cell->character == str[i] &&
            cell->color == fg_color &&
            cell->bg_color == bg &&
            cell->attributes == attr) {
            continue;
        }

        cell->character = str[i];
        cell->color = fg_color;
        cell->bg_color = bg;
        cell->attributes = attr;
        update_cell(x + i, y);
        refresh_line_drawing_cells_around(x + i, y);
    }

    if (write_len > 0) {
        cursor_x = x + write_len - 1;
        if (cursor_x >= grid_cols) {
            cursor_x = 0;
            cursor_y = (cursor_y + 1) % grid_rows;
        }
    }
}

void text_mode_print_at_attr(int x, int y, const char *str, uint8_t color, uint8_t attr) {
    text_mode_write_cells(x, y, str, color, bg_color, attr);
}

void text_mode_print_at_attr_bg(int x, int y, const char *str, uint8_t fg_color, uint8_t bg, uint8_t attr) {
    text_mode_write_cells(x, y, str, fg_color, bg, attr);
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
    if (saved_font < 0 || saved_font >= FONT_COUNT) saved_font = FONT_HACK_8;

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

static uint32_t vlw_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static const uint8_t *vlw_find_glyph(const uint8_t *data, size_t size, uint16_t unicode,
                                      int *out_width, int *out_height,
                                      int *out_top_offset, int *out_left_offset) {
    if (!data || size < 24) return NULL;
    uint32_t glyph_count = vlw_read_be32(data);
    if (glyph_count == 0) return NULL;

    uint32_t bitmap_offset = 24 + glyph_count * 28;
    for (uint32_t i = 0; i < glyph_count; i++) {
        const uint8_t *m = data + 24 + i * 28;
        if (m + 28 > data + size) return NULL;
        uint32_t gunicode = vlw_read_be32(m);
        uint32_t height = vlw_read_be32(m + 4);
        uint32_t width = vlw_read_be32(m + 8);
        if (gunicode == unicode) {
            if (out_width) *out_width = (int)width;
            if (out_height) *out_height = (int)height;
            if (out_top_offset) *out_top_offset = (int)(int32_t)vlw_read_be32(m + 16);
            if (out_left_offset) *out_left_offset = (int)(int32_t)vlw_read_be32(m + 20);
            if (width > 0 && height > 0 && bitmap_offset + width * height <= size) {
                return data + bitmap_offset;
            }
            return NULL;
        }
        bitmap_offset += width * height;
    }
    return NULL;
}

bool text_mode_save_screenshot(void) {
    if (!initialized || !grid) return false;
    if (!sd_card_is_mounted()) return false;

    mkdir("/sdcard/screenshots", 0777);

    char path[64];
    int num = 0;
    FILE *existing;
    do {
        snprintf(path, sizeof(path), "/sdcard/screenshots/shot_%03d", num);
        char try_path[72];
        snprintf(try_path, sizeof(try_path), "%s.ppm", path);
        existing = fopen(try_path, "r");
        if (existing) {
            fclose(existing);
            num++;
        }
    } while (existing && num < 1000);
    if (num >= 1000) return false;

    char ppm_path[72];
    snprintf(ppm_path, sizeof(ppm_path), "%s.ppm", path);
    FILE *fppm = fopen(ppm_path, "wb");
    if (!fppm) return false;

    int disp_w = display_get_width();
    int disp_h = display_get_height();
    fprintf(fppm, "P6\n%d %d\n255\n", disp_w, disp_h);

    uint8_t *row_buf = (uint8_t *)malloc((size_t)disp_w * 3);
    if (!row_buf) {
        fclose(fppm);
        return false;
    }

    int fw = font_width;
    int fh = font_height;
    int mid_x = fw / 2;
    int mid_y = fh / 2;
    int max_gx = grid_cols;
    int max_gy = grid_rows;

    for (int py = 0; py < disp_h; py++) {
        int gy = py / fh;
        int char_row = py % fh;
        uint8_t *p = row_buf;

        if (gy >= max_gy) {
            memset(row_buf, 0, (size_t)disp_w * 3);
            fwrite(row_buf, 1, (size_t)disp_w * 3, fppm);
            continue;
        }

        for (int gx = 0; gx < max_gx; gx++) {
            text_cell_t *cell = &grid[gy * max_gx + gx];
            uint8_t fg_idx = cell->color & 0x0F;
            uint8_t bg_idx = cell->bg_color & 0x0F;
            uint8_t attrs = cell->attributes;

            if (attrs & TEXT_ATTR_INVERSE) {
                uint8_t tmp = fg_idx;
                fg_idx = bg_idx;
                bg_idx = tmp;
            }

            uint16_t rgb565_fg = color_palette[fg_idx];
            uint16_t rgb565_bg = color_palette[bg_idx];
            uint8_t r_fg = (rgb565_fg >> 8) & 0xF8; r_fg |= r_fg >> 5;
            uint8_t g_fg = (rgb565_fg >> 3) & 0xFC; g_fg |= g_fg >> 6;
            uint8_t b_fg = (rgb565_fg << 3) & 0xF8; b_fg |= b_fg >> 5;
            uint8_t r_bg = (rgb565_bg >> 8) & 0xF8; r_bg |= r_bg >> 5;
            uint8_t g_bg = (rgb565_bg >> 3) & 0xFC; g_bg |= g_bg >> 6;
            uint8_t b_bg = (rgb565_bg << 3) & 0xF8; b_bg |= b_bg >> 5;

            bool line_drawing = (attrs & TEXT_ATTR_LINE_DRAWING) != 0;
            uint8_t line_mask = 0;
            if (line_drawing) {
                line_mask = line_drawing_mask_for_cell(gx, gy);
            }

            // Determine font variant from attributes
            font_variant_t variant = FONT_VARIANT_REGULAR;
            if ((attrs & TEXT_ATTR_BOLD) && (attrs & TEXT_ATTR_ITALIC))
                variant = FONT_VARIANT_BOLDITALIC;
            else if (attrs & TEXT_ATTR_BOLD)
                variant = FONT_VARIANT_BOLD;
            else if (attrs & TEXT_ATTR_ITALIC)
                variant = FONT_VARIANT_ITALIC;

            // Load VLW data for this variant and find glyph
            size_t var_size = 0;
            const uint8_t *var_data = font_get_variant_data(current_font, variant, &var_size);
            int gw = 0, gh = 0, top_offset = 0, left_offset = 0;
            const uint8_t *bitmap = NULL;
            int ascent = 0;
            if (var_data) {
                ascent = (int)vlw_read_be32(var_data + 16);
                if (cell->character != ' ') {
                    bitmap = vlw_find_glyph(var_data, var_size, (unsigned char)cell->character,
                                            &gw, &gh, &top_offset, &left_offset);
                }
            }

            for (int dx = 0; dx < fw; dx++) {
                uint8_t alpha = 0;
                if (line_drawing && line_mask != 0) {
                    bool set = false;
                    if ((line_mask & LINE_DRAW_LEFT) && (line_mask & LINE_DRAW_RIGHT) && char_row == mid_y)
                        set = true;
                    else if ((line_mask & LINE_DRAW_LEFT) && !(line_mask & LINE_DRAW_RIGHT) && char_row == mid_y && dx <= mid_x)
                        set = true;
                    else if ((line_mask & LINE_DRAW_RIGHT) && !(line_mask & LINE_DRAW_LEFT) && char_row == mid_y && dx >= mid_x)
                        set = true;
                    else if ((line_mask & LINE_DRAW_UP) && (line_mask & LINE_DRAW_DOWN) && dx == mid_x)
                        set = true;
                    else if ((line_mask & LINE_DRAW_UP) && !(line_mask & LINE_DRAW_DOWN) && dx == mid_x && char_row <= mid_y)
                        set = true;
                    else if ((line_mask & LINE_DRAW_DOWN) && !(line_mask & LINE_DRAW_UP) && dx == mid_x && char_row >= mid_y)
                        set = true;
                    if (set) alpha = 255;
                } else if (bitmap && gw > 0 && gh > 0) {
                    int glyph_row = char_row - (ascent - top_offset);
                    int glyph_col = dx - left_offset;
                    if (glyph_row >= 0 && glyph_row < gh && glyph_col >= 0 && glyph_col < gw) {
                        alpha = bitmap[glyph_row * gw + glyph_col];
                    }
                }

                if (alpha < 255 && (attrs & TEXT_ATTR_UNDERLINE) && char_row == fh - 1)
                    alpha = 255;
                if (alpha < 255 && (attrs & TEXT_ATTR_BORDER_TOP) && char_row == 0)
                    alpha = 255;
                if (alpha < 255 && (attrs & TEXT_ATTR_BORDER_LEFT) && dx == 0)
                    alpha = 255;
                if (alpha < 255 && (attrs & TEXT_ATTR_BORDER_RIGHT) && dx == fw - 1)
                    alpha = 255;

                int ia = alpha;
                int ina = 255 - ia;
                *p++ = (r_fg * ia + r_bg * ina) / 255;
                *p++ = (g_fg * ia + g_bg * ina) / 255;
                *p++ = (b_fg * ia + b_bg * ina) / 255;
            }
        }

        fwrite(row_buf, 1, (size_t)disp_w * 3, fppm);
    }

    free(row_buf);
    fclose(fppm);
    ESP_LOGI(TAG, "Screenshot saved: %s.ppm", path);
    return true;
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
