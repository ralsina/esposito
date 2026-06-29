#include "ui2_graphical.h"
#include "text_mode.h"
#include "hardware.h"
#include "os_core.h"

#define UI2_GRAPHICAL_AUTO (-1)

static int graphical_mode = UI2_GRAPHICAL_AUTO;

void ui2_set_graphical(bool graphical) {
    graphical_mode = graphical ? 1 : 0;
}

bool ui2_is_graphical(void) {
    if (graphical_mode == UI2_GRAPHICAL_AUTO) {
        graphical_mode = os_has_capability("psram") ? 1 : 0;
    }
    return graphical_mode == 1;
}

int ui2_graphical_px(int cells) {
    return cells * text_mode_get_char_width();
}

int ui2_graphical_py(int cells) {
    return cells * text_mode_get_char_height();
}

int ui2_graphical_pw(int cells) {
    return cells * text_mode_get_char_width();
}

int ui2_graphical_ph(int cells) {
    return cells * text_mode_get_char_height();
}

uint16_t ui2_graphical_color(uint8_t palette_index) {
    return text_mode_get_palette_color(palette_index & 0x0F);
}

void ui2_draw_rounded_rect(int px, int py, int pw, int ph, int r,
                           uint16_t fill, uint16_t border) {
    if (pw <= 0 || ph <= 0 || r <= 0) {
        display_fill_rect(px, py, pw, ph, fill);
        return;
    }
    if (r > pw / 2) r = pw / 2;
    if (r > ph / 2) r = ph / 2;

    // Fill center
    display_fill_rect(px + r, py + r, pw - 2 * r, ph - 2 * r, fill);
    // Top band
    display_fill_rect(px + r, py, pw - 2 * r, r, fill);
    // Bottom band
    display_fill_rect(px + r, py + ph - r, pw - 2 * r, r, fill);
    // Left band
    display_fill_rect(px, py + r, r, ph - 2 * r, fill);
    // Right band
    display_fill_rect(px + pw - r, py + r, r, ph - 2 * r, fill);

    // Fill corner arcs
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                display_draw_pixel(px + dx, py + dy, fill);
                display_draw_pixel(px + pw - 1 - dx, py + dy, fill);
                display_draw_pixel(px + dx, py + ph - 1 - dy, fill);
                display_draw_pixel(px + pw - 1 - dx, py + ph - 1 - dy, fill);
            }
        }
    }

    // Draw border — straight edges
    for (int dx = r; dx < pw - r; dx++) {
        display_draw_pixel(px + dx, py, border);
        display_draw_pixel(px + dx, py + ph - 1, border);
    }
    for (int dy = r; dy < ph - r; dy++) {
        display_draw_pixel(px, py + dy, border);
        display_draw_pixel(px + pw - 1, py + dy, border);
    }

    // Border corner arcs — only the outermost ring
    for (int dy = 0; dy <= r; dy++) {
        for (int dx = 0; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            int outer = r * r;
            int inner = (r - 1) * (r - 1);
            if (d2 > inner && d2 <= outer) {
                display_draw_pixel(px + dx, py + dy, border);
                display_draw_pixel(px + pw - 1 - dx, py + dy, border);
                display_draw_pixel(px + dx, py + ph - 1 - dy, border);
                display_draw_pixel(px + pw - 1 - dx, py + ph - 1 - dy, border);
            }
        }
    }
}

void ui2_fill_rounded_rect(int px, int py, int pw, int ph, int r,
                           uint16_t fill) {
    if (pw <= 0 || ph <= 0) return;
    if (r <= 0 || r > pw / 2 || r > ph / 2) {
        display_fill_rect(px, py, pw, ph, fill);
        return;
    }

    display_fill_rect(px + r, py + r, pw - 2 * r, ph - 2 * r, fill);
    display_fill_rect(px + r, py, pw - 2 * r, r, fill);
    display_fill_rect(px + r, py + ph - r, pw - 2 * r, r, fill);
    display_fill_rect(px, py + r, r, ph - 2 * r, fill);
    display_fill_rect(px + pw - r, py + r, r, ph - 2 * r, fill);

    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                display_draw_pixel(px + dx, py + dy, fill);
                display_draw_pixel(px + pw - 1 - dx, py + dy, fill);
                display_draw_pixel(px + dx, py + ph - 1 - dy, fill);
                display_draw_pixel(px + pw - 1 - dx, py + ph - 1 - dy, fill);
            }
        }
    }
}

uint16_t ui2_lighten_color(uint16_t color) {
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;
    r = r + ((0x1F - r) >> 1);
    g = g + ((0x3F - g) >> 1);
    b = b + ((0x1F - b) >> 1);
    return (r << 11) | (g << 5) | b;
}

uint16_t ui2_darken_color(uint16_t color) {
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;
    r = r >> 1;
    g = g >> 1;
    b = b >> 1;
    return (r << 11) | (g << 5) | b;
}
