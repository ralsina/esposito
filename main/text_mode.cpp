#include "text_mode.h"
#include "hardware.h"
#include "graphics_mode.h"
#include "fonts.h"
#include "sd_card.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

extern "C" {
    #include "app_config.h"
}

static const char *TAG = "text_mode";

static uint16_t color_palette[16] = {
    0x0000, 0x0010, 0x0400, 0x0410, 0x8000, 0x8010, 0x8400, 0x8410,
    0x4208, 0x001F, 0x07E0, 0x07FF, 0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

static text_cell_t *grid = NULL;
static int grid_cols = TEXT_MODE_COLS;
static int grid_rows = TEXT_MODE_ROWS;
static int grid_stride = 0;
static int grid_capacity = 0;
static int font_width = TEXT_MODE_CHAR_WIDTH;
static int font_height = TEXT_MODE_CHAR_HEIGHT;
static font_id_t current_font = FONT_BOOT;
static font_variant_t current_variant = FONT_VARIANT_REGULAR;

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t bg_color = 0;
static bool initialized = false;
static bool graphics = false;

static void update_cell(int x, int y);

static const uint8_t *vlw_find_glyph(const uint8_t *data, size_t size, uint16_t unicode,
                                      int *out_width, int *out_height,
                                      int *out_top_offset, int *out_left_offset);

enum line_drawing_mask_t {
    LINE_DRAW_LEFT = 1 << 0,
    LINE_DRAW_RIGHT = 1 << 1,
    LINE_DRAW_UP = 1 << 2,
    LINE_DRAW_DOWN = 1 << 3,
};

static uint8_t codepoint_to_mask(uint16_t cp) {
    switch (cp) {
        case 0x2500: return LINE_DRAW_LEFT | LINE_DRAW_RIGHT;
        case 0x2502: return LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 0x250C: return LINE_DRAW_RIGHT | LINE_DRAW_DOWN;
        case 0x2510: return LINE_DRAW_LEFT | LINE_DRAW_DOWN;
        case 0x2514: return LINE_DRAW_RIGHT | LINE_DRAW_UP;
        case 0x2518: return LINE_DRAW_LEFT | LINE_DRAW_UP;
        case 0x251C: return LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 0x2524: return LINE_DRAW_LEFT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        case 0x252C: return LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_DOWN;
        case 0x2534: return LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP;
        case 0x253C: return LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN;
        default: return 0;
    }
}

static bool cell_is_symbol(const text_cell_t *cell) {
    return cell && (cell->attributes & TEXT_ATTR_SYMBOL);
}

static bool is_box_drawing(uint16_t cp) {
    return cp >= 0x2500 && cp <= 0x257F;
}

static uint16_t vt100_to_unicode(char ch) {
    switch (ch) {
        case '-': case 'q': return 0x2500;
        case '|': case 'x': return 0x2502;
        case 'l': return 0x250C;
        case 'k': return 0x2510;
        case 'm': return 0x2514;
        case 'j': return 0x2518;
        case 't': return 0x251C;
        case 'u': return 0x2524;
        case 'w': return 0x252C;
        case 'v': return 0x2534;
        case 'n': return 0x253C;
        case '+': return 0x253C;
        default: return (uint16_t)(uint8_t)ch;
    }
}

static uint16_t mask_to_box_char(uint8_t mask) {
    if (mask == (LINE_DRAW_LEFT | LINE_DRAW_RIGHT)) return 0x2500;
    if (mask == (LINE_DRAW_UP | LINE_DRAW_DOWN)) return 0x2502;
    if (mask == (LINE_DRAW_RIGHT | LINE_DRAW_DOWN)) return 0x250C;
    if (mask == (LINE_DRAW_LEFT | LINE_DRAW_DOWN)) return 0x2510;
    if (mask == (LINE_DRAW_RIGHT | LINE_DRAW_UP)) return 0x2514;
    if (mask == (LINE_DRAW_LEFT | LINE_DRAW_UP)) return 0x2518;
    if (mask == (LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN)) return 0x251C;
    if (mask == (LINE_DRAW_LEFT | LINE_DRAW_UP | LINE_DRAW_DOWN)) return 0x2524;
    if (mask == (LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_DOWN)) return 0x252C;
    if (mask == (LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP)) return 0x2534;
    if (mask == (LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN)) return 0x253C;
    return 0x253C;
}

static bool neighbor_connects_to_direction(int nx, int ny, uint8_t opposite_mask) {
    if (!grid || nx < 0 || ny < 0 || nx >= grid_cols || ny >= grid_rows) return false;
    const text_cell_t *neighbor = &grid[ny * grid_cols + nx];
    if (!cell_is_symbol(neighbor)) return false;
    return (codepoint_to_mask(neighbor->character) & opposite_mask) != 0;
}

static uint16_t resolve_box_char(int x, int y) {
    if (!grid || x < 0 || y < 0 || x >= grid_cols || y >= grid_rows) return '?';
    const text_cell_t *cell = &grid[y * grid_cols + x];
    if (!cell_is_symbol(cell) || !is_box_drawing(cell->character)) return cell->character;

    uint8_t base_mask = codepoint_to_mask(cell->character);
    if (base_mask == (LINE_DRAW_LEFT | LINE_DRAW_RIGHT | LINE_DRAW_UP | LINE_DRAW_DOWN)) {
        uint8_t mask = 0;
        if (neighbor_connects_to_direction(x - 1, y, LINE_DRAW_RIGHT)) mask |= LINE_DRAW_LEFT;
        if (neighbor_connects_to_direction(x + 1, y, LINE_DRAW_LEFT)) mask |= LINE_DRAW_RIGHT;
        if (neighbor_connects_to_direction(x, y - 1, LINE_DRAW_DOWN)) mask |= LINE_DRAW_UP;
        if (neighbor_connects_to_direction(x, y + 1, LINE_DRAW_UP)) mask |= LINE_DRAW_DOWN;
        if (mask == 0) mask = base_mask;
        return mask_to_box_char(mask);
    }
    return cell->character;
}

static void refresh_symbol_cells_around(int x, int y) {
    static const int offsets[][2] = {{0,0}, {-1,0}, {1,0}, {0,-1}, {0,1}};
    for (size_t index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
        int nx = x + offsets[index][0];
        int ny = y + offsets[index][1];
        if (!grid || nx < 0 || ny < 0 || nx >= grid_cols || ny >= grid_rows) continue;
        text_cell_t *cell = &grid[ny * grid_cols + nx];
        if (cell->attributes & TEXT_ATTR_SYMBOL) {
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
    if (graphics_mode_is_active()) return;
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

        uint16_t cp = cell->character;
        if (cell->attributes & TEXT_ATTR_SYMBOL) {
            cp = resolve_box_char(x, y);
        }

        size_t var_size = 0;
        const uint8_t *var_data = font_get_variant_data(current_font, current_variant, &var_size);
        int gw = 0, gh = 0, gtop = 0, gleft = 0;
        const uint8_t *glyph = vlw_find_glyph(var_data, var_size, cp, &gw, &gh, &gtop, &gleft);
        if (glyph) {
            display_draw_unicode_at(px, py, cp, fg, bg);
        } else {
            size_t sup_size = 0;
            const uint8_t *sup_data = font_get_supplement_data(current_font, &sup_size);
            glyph = vlw_find_glyph(sup_data, sup_size, cp, &gw, &gh, &gtop, &gleft);
            if (glyph) {
                display_draw_unicode_with_font(px, py, cp, fg, bg, sup_data, sup_size);
                display_load_font(current_font, current_variant);
            } else {
                display_draw_unicode_at(px, py, '?', fg, bg);
            }
        }
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

static void update_cell_range(int x, int y, int count) {
    if (graphics_mode_is_active()) return;
    if (!grid || count < 1 || x < 0 || x + count > grid_cols || y < 0 || y >= grid_rows) return;

    int start_idx = y * grid_cols + x;
    text_cell_t *first = &grid[start_idx];

    uint16_t fg = color_palette[first->color & 0x0F];
    uint16_t bg = color_palette[first->bg_color & 0x0F];
    uint8_t attr = first->attributes;

    if (attr & TEXT_ATTR_INVERSE) {
        uint16_t tmp = fg; fg = bg; bg = tmp;
    }

    font_variant_t needed = FONT_VARIANT_REGULAR;
    if (attr & TEXT_ATTR_BOLD && attr & TEXT_ATTR_ITALIC) {
        needed = FONT_VARIANT_BOLDITALIC;
    } else if (attr & TEXT_ATTR_BOLD) {
        needed = FONT_VARIANT_BOLD;
    } else if (attr & TEXT_ATTR_ITALIC) {
        needed = FONT_VARIANT_ITALIC;
    }
    if (needed != current_variant) {
        if (display_load_font(current_font, needed)) {
            current_variant = needed;
        }
    }

    int px, py;
    grid_to_pixel(x, y, &px, &py);

    // Build text string and check for symbol chars
    char buf[256];
    int buf_len = 0;
    bool has_symbols = false;
    for (int i = 0; i < count; i++) {
        text_cell_t *cell = &grid[start_idx + i];
        if (cell->attributes & TEXT_ATTR_SYMBOL) {
            has_symbols = true;
            break;
        }
        uint16_t cp = cell->character;
        if (cp < 0x80) {
            buf[buf_len++] = (char)cp;
        } else if (cp < 0x800) {
            buf[buf_len++] = (char)(0xC0 | (cp >> 6));
            buf[buf_len++] = (char)(0x80 | (cp & 0x3F));
        } else {
            buf[buf_len++] = (char)(0xE0 | (cp >> 12));
            buf[buf_len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[buf_len++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    buf[buf_len] = '\0';

    if (has_symbols) {
        for (int i = 0; i < count; i++) {
            update_cell(x + i, y);
        }
        return;
    }

    // Draw background for the whole run
    display_fill_rect(px, py, font_width * count, font_height, bg);

    // Draw text (transparent so it doesn't overwrite our fill_rect background)
    if (buf_len > 0) {
        display_draw_text_transparent(px, py, buf, fg);
    }

    // Draw borders (single rect per border type for the run)
    if (attr & TEXT_ATTR_UNDERLINE) {
        display_fill_rect(px, py + font_height - 1, font_width * count, 1, fg);
    }
    if (attr & TEXT_ATTR_BORDER_TOP) {
        display_fill_rect(px, py, font_width * count, 1, fg);
    }
    if (attr & TEXT_ATTR_BORDER_LEFT) {
        display_fill_rect(px, py, 1, font_height, fg);
    }
    if (attr & TEXT_ATTR_BORDER_RIGHT) {
        display_fill_rect(px + font_width * count - 1, py, 1, font_height, fg);
    }
}

void text_mode_set_palette(const uint16_t colors[16]) {
    if (!colors) return;
    memcpy(color_palette, colors, sizeof(color_palette));
    graphics_set_palette(colors, 16);
    if (grid && initialized) {
        for (int y = 0; y < grid_rows; y++) {
            for (int x = 0; x < grid_cols; x++) {
                update_cell(x, y);
            }
        }
    }
}

static const uint16_t palette_cga[16] = {
    0x0000, 0x0010, 0x0400, 0x0410,
    0x8000, 0x8010, 0x8400, 0x8410,
    0x4208, 0x001F, 0x07E0, 0x07FF,
    0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

static const uint16_t palette_cga_light[16] = {
    0xFFBC, 0x0013, 0x0440, 0x0453,
    0x8800, 0x8813, 0x8840, 0x0000,
    0x8410, 0x001F, 0x07E0, 0x07FF,
    0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

static const uint16_t palette_solarized_dark[16] = {
    0x0146, 0x6B98, 0x84C0, 0x2D13,
    0xD985, 0xD1B0, 0xB440, 0x84B2,
    0x01A8, 0x245A, 0x07E0, 0x63D0,
    0xEF5A, 0xCA42, 0x5B6E, 0x9514,
};

static const uint16_t palette_solarized_light[16] = {
    0xFFBC, 0x6B98, 0x84C0, 0x2D13,
    0xD985, 0xD1B0, 0xB440, 0x63D0,
    0xEF5A, 0x245A, 0xCA42, 0x5B6E,
    0x0146, 0x01A8, 0x01A8, 0x84B2,
};

static const uint16_t *all_palettes[] = {
    palette_cga, palette_cga_light, palette_solarized_dark, palette_solarized_light,
};

#define NUM_PALETTES (sizeof(all_palettes) / sizeof(all_palettes[0]))

void text_mode_apply_configured_palette(int index) {
    if (index < 0 || index >= (int)NUM_PALETTES) {
        index = 0;
    }
    text_mode_set_palette(all_palettes[index]);
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

    if (font < 0 || font >= font_count) font = FONT_BOOT;
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
        grid_stride = 0;
        grid_capacity = 0;
        font_width = TEXT_MODE_CHAR_WIDTH;
        font_height = TEXT_MODE_CHAR_HEIGHT;
        current_font = FONT_BOOT;
        current_variant = FONT_VARIANT_REGULAR;
        return false;
    }

    grid_stride = max_cols;
    grid_capacity = max_cells;

    display_load_font(font, FONT_VARIANT_REGULAR);

    ESP_LOGI(TAG, "Grid allocated: %dx%d (max %dx%d), font: %s (%dx%d)",
             grid_cols, grid_rows, max_cols, max_rows,
             font_table[font].name, font_width, font_height);
    return true;
}

bool text_mode_init_ex(font_id_t font) {
    if (font < 0 || font >= font_count) font = FONT_BOOT;

    if (!init_grid(font)) return false;

    // Deactivate graphics mode if it was active
    graphics_mode_deinit();

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
    extern app_context_t *os_get_current_app(void);

    // os_settings_get_string unbinds config; save the current app's binding to restore it
    const char *saved_app = NULL;
    app_context_t *ctx = os_get_current_app();
    if (ctx) saved_app = ctx->name;

    size_t len = os_settings_get_string("system/default_font", "hack 8", font_setting, sizeof(font_setting));

    if (saved_app) {
        config_bind_app(saved_app);
    }

    font_id_t default_font = font_lookup_by_name(font_setting);
    if (default_font < 0 || default_font >= font_count) {
        default_font = FONT_BOOT;
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
    ESP_LOGI(TAG, "Font lookup result: %d (font_count=%d)", (int)configured_font, font_count);

    if (configured_font < 0 || configured_font >= font_count) {
        ESP_LOGW(TAG, "Invalid font ID %d, falling back to boot font", (int)configured_font);
        configured_font = FONT_BOOT;
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

    if (font < 0 || font >= font_count) {
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
            int idx = y * grid_stride + x;
            if (idx < grid_capacity) {
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
            int idx = y * grid_stride + x;
            if (idx < grid_capacity) {
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

static int utf8_decode(const char *str, uint16_t *out_cp) {
    uint8_t b0 = (uint8_t)str[0];
    if (b0 < 0x80) {
        *out_cp = b0;
        return 1;
    } else if ((b0 & 0xE0) == 0xC0) {
        *out_cp = (uint16_t)((b0 & 0x1F) << 6) | (uint8_t)(str[1] & 0x3F);
        return 2;
    } else if ((b0 & 0xF0) == 0xE0) {
        *out_cp = (uint16_t)((b0 & 0x0F) << 12) | ((uint8_t)(str[1] & 0x3F) << 6) | (uint8_t)(str[2] & 0x3F);
        return 3;
    }
    *out_cp = '?';
    return 1;
}

static void text_mode_write_cells(int x, int y, const char *str, uint8_t fg_color, uint8_t bg, uint8_t attr) {
    if (!initialized || !grid || !str) return;
    if (x < 0 || x >= grid_cols || y < 0 || y >= grid_rows) return;

    int len = strlen(str);
    int cx = x;
    int max_x = grid_cols;
    const char *p = str;
    const char *end = str + len;

    int run_start = -1;
    int run_len = 0;

    while (p < end && cx < max_x) {
        uint16_t cp;
        int bytes = utf8_decode(p, &cp);
        if (p + bytes > end) break;

        if (attr & TEXT_ATTR_SYMBOL) {
            cp = vt100_to_unicode((char)cp);
        }

        int idx = y * grid_cols + cx;
        text_cell_t *cell = &grid[idx];

        if (cell->character == cp &&
            cell->color == fg_color &&
            cell->bg_color == bg &&
            cell->attributes == attr) {
            // Already correct - flush accumulated run and skip
            if (run_len > 0) {
                update_cell_range(run_start, y, run_len);
                refresh_symbol_cells_around(run_start, y);
                if (run_len > 1) {
                    refresh_symbol_cells_around(run_start + run_len - 1, y);
                }
                run_len = 0;
            }
            cx++;
            p += bytes;
            continue;
        }

        cell->character = cp;
        cell->color = fg_color;
        cell->bg_color = bg;
        cell->attributes = attr;

        if (attr & TEXT_ATTR_SYMBOL) {
            // Symbol chars need per-cell rendering, flush any accumulated run
            if (run_len > 0) {
                update_cell_range(run_start, y, run_len);
                refresh_symbol_cells_around(run_start, y);
                if (run_len > 1) {
                    refresh_symbol_cells_around(run_start + run_len - 1, y);
                }
                run_len = 0;
            }
            update_cell(cx, y);
            refresh_symbol_cells_around(cx, y);
        } else {
            if (run_len == 0) {
                run_start = cx;
                run_len = 1;
            } else {
                run_len++;
            }
            refresh_symbol_cells_around(cx, y);
        }

        cx++;
        p += bytes;
    }

    // Flush remaining run
    if (run_len > 0) {
        update_cell_range(run_start, y, run_len);
        refresh_symbol_cells_around(run_start, y);
        if (run_len > 1) {
            refresh_symbol_cells_around(run_start + run_len - 1, y);
        }
    }

    if (cx > x) {
        cursor_x = cx - 1;
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
    int max_gx = grid_cols;
    int max_gy = grid_rows;

    for (int py = 0; py < disp_h; py++) {
        int gy = py / fh;
        int char_row = py % fh;

        if (gy >= max_gy) {
            memset(row_buf, 0, (size_t)disp_w * 3);
            fwrite(row_buf, 1, (size_t)disp_w * 3, fppm);
            continue;
        }

        memset(row_buf, 0, (size_t)disp_w * 3);

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

            bool is_symbol = (attrs & TEXT_ATTR_SYMBOL) != 0;
            uint16_t render_cp = cell->character;
            if (is_symbol) {
                render_cp = resolve_box_char(gx, gy);
            }

            font_variant_t variant = FONT_VARIANT_REGULAR;
            if ((attrs & TEXT_ATTR_BOLD) && (attrs & TEXT_ATTR_ITALIC))
                variant = FONT_VARIANT_BOLDITALIC;
            else if (attrs & TEXT_ATTR_BOLD)
                variant = FONT_VARIANT_BOLD;
            else if (attrs & TEXT_ATTR_ITALIC)
                variant = FONT_VARIANT_ITALIC;

            size_t var_size = 0;
            const uint8_t *var_data = font_get_variant_data(current_font, variant, &var_size);
            int gw = 0, gh = 0, top_offset = 0, left_offset = 0;
            const uint8_t *bitmap = NULL;
            int ascent = 0;
            if (var_data) {
                ascent = (int)vlw_read_be32(var_data + 16);
                if (render_cp != ' ') {
                    bitmap = vlw_find_glyph(var_data, var_size, render_cp,
                                            &gw, &gh, &top_offset, &left_offset);
                }
            }
            if (!bitmap && render_cp != ' ') {
                size_t sup_size = 0;
                const uint8_t *sup_data = font_get_supplement_data(current_font, &sup_size);
                if (sup_data) {
                    bitmap = vlw_find_glyph(sup_data, sup_size, render_cp,
                                            &gw, &gh, &top_offset, &left_offset);
                }
            }
            if (!bitmap && render_cp != ' ') {
                var_data = font_get_variant_data(current_font, variant, &var_size);
                bitmap = vlw_find_glyph(var_data, var_size, '?',
                                        &gw, &gh, &top_offset, &left_offset);
            }

            int cell_px = gx * fw;
            int glyph_row = -1;
            if (bitmap && gw > 0 && gh > 0) {
                glyph_row = char_row - (ascent - top_offset);
            }

            // Fill cell background (fw pixels from cell_px)
            for (int dx = 0; dx < fw; dx++) {
                int out_px = cell_px + dx;
                if (out_px < 0 || out_px >= disp_w) continue;
                uint8_t *dst = row_buf + out_px * 3;
                dst[0] = r_bg; dst[1] = g_bg; dst[2] = b_bg;
            }

            // Overlay glyph at natural metrics, clipped to display clip rect:
            // cell_px-1 .. cell_px+fw (fw+2 pixels wide, matching TFT_eSPI)
            if (bitmap && glyph_row >= 0 && glyph_row < gh) {
                int clip_l = (cell_px - 1 > 0) ? (cell_px - 1) : 0;
                int clip_r = (cell_px + fw < disp_w - 1) ? (cell_px + fw) : (disp_w - 1);
                int glyph_base = cell_px + left_offset;

                for (int gc = 0; gc < gw; gc++) {
                    int out_px = glyph_base + gc;
                    if (out_px < clip_l || out_px > clip_r) continue;
                    uint8_t alpha = bitmap[glyph_row * gw + gc];

                    // Apply border attributes only within cell bounds
                    int dx = out_px - cell_px;
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
                    uint8_t *dst = row_buf + out_px * 3;
                    dst[0] = (uint8_t)((r_fg * ia + dst[0] * ina) / 255);
                    dst[1] = (uint8_t)((g_fg * ia + dst[1] * ina) / 255);
                    dst[2] = (uint8_t)((b_fg * ia + dst[2] * ina) / 255);
                }
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

text_mode_snapshot_t* text_mode_save_snapshot(void) {
    if (!initialized || !grid) {
        return NULL;
    }

    text_mode_snapshot_t *snapshot = (text_mode_snapshot_t*)malloc(sizeof(text_mode_snapshot_t));
    if (!snapshot) {
        return NULL;
    }

    // Save grid dimensions and state
    snapshot->cols = grid_cols;
    snapshot->rows = grid_rows;
    snapshot->cursor_x = cursor_x;
    snapshot->cursor_y = cursor_y;
    snapshot->bg_color = bg_color;
    snapshot->font = current_font;
    snapshot->variant = current_variant;

    // Allocate and copy grid cells
    size_t grid_size = grid_cols * grid_rows * sizeof(text_cell_t);
    snapshot->cells = (text_cell_t*)malloc(grid_size);
    if (!snapshot->cells) {
        free(snapshot);
        return NULL;
    }

    memcpy(snapshot->cells, grid, grid_size);

    ESP_LOGI(TAG, "Saved snapshot: %dx%d grid", grid_cols, grid_rows);
    return snapshot;
}

void text_mode_restore_snapshot(text_mode_snapshot_t *snapshot) {
    if (!snapshot || !snapshot->cells) {
        ESP_LOGW(TAG, "Cannot restore NULL snapshot");
        return;
    }

    // Check if grid dimensions match
    if (snapshot->cols != grid_cols || snapshot->rows != grid_rows) {
        ESP_LOGW(TAG, "Grid dimensions changed: was %dx%d, now %dx%d. Reinitializing grid.",
                 snapshot->cols, snapshot->rows, grid_cols, grid_rows);

        // Free current grid and allocate new one with snapshot dimensions
        if (grid) {
            free(grid);
        }

        grid_cols = snapshot->cols;
        grid_rows = snapshot->rows;
        size_t grid_size = grid_cols * grid_rows * sizeof(text_cell_t);
        grid = (text_cell_t*)malloc(grid_size);
        if (!grid) {
            ESP_LOGE(TAG, "Failed to allocate grid for restore");
            return;
        }
    }

    // Copy grid cells back
    size_t grid_size = grid_cols * grid_rows * sizeof(text_cell_t);
    memcpy(grid, snapshot->cells, grid_size);

    // Restore cursor position and background color
    cursor_x = snapshot->cursor_x;
    cursor_y = snapshot->cursor_y;
    bg_color = snapshot->bg_color;

    // Restore font if needed
    if (snapshot->font != current_font || snapshot->variant != current_variant) {
        if (display_load_font(snapshot->font, snapshot->variant)) {
            current_font = snapshot->font;
            current_variant = snapshot->variant;

            // Update font dimensions
            font_width = font_table[current_font].char_width;
            font_height = font_table[current_font].char_height;
        }
    }

    // Redraw all cells
    for (int y = 0; y < grid_rows; y++) {
        for (int x = 0; x < grid_cols; x++) {
            update_cell(x, y);
        }
    }

    ESP_LOGI(TAG, "Restored snapshot: %dx%d grid", grid_cols, grid_rows);
}

void text_mode_free_snapshot(text_mode_snapshot_t *snapshot) {
    if (!snapshot) {
        return;
    }

    if (snapshot->cells) {
        free(snapshot->cells);
    }

    free(snapshot);
}

void text_mode_pixel_to_cell(int pixel_x, int pixel_y, int *cell_x, int *cell_y) {
    if (!cell_x || !cell_y) {
        return;
    }

    *cell_x = pixel_x / font_width;
    *cell_y = pixel_y / font_height;

    // Clamp to grid bounds
    if (*cell_x < 0) *cell_x = 0;
    if (*cell_x >= grid_cols) *cell_x = grid_cols - 1;
    if (*cell_y < 0) *cell_y = 0;
    if (*cell_y >= grid_rows) *cell_y = grid_rows - 1;
}

void text_mode_cell_to_pixel(int cell_x, int cell_y, int *pixel_x, int *pixel_y) {
    if (!pixel_x || !pixel_y) {
        return;
    }

    // Clamp cell coordinates to grid bounds
    if (cell_x < 0) cell_x = 0;
    if (cell_x >= grid_cols) cell_x = grid_cols - 1;
    if (cell_y < 0) cell_y = 0;
    if (cell_y >= grid_rows) cell_y = grid_rows - 1;

    *pixel_x = cell_x * font_width;
    *pixel_y = cell_y * font_height;
}
