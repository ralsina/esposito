#include "paint_render.h"
#include "graphics_mode.h"
#include "hardware.h"
#include <stdio.h>
#include <string.h>

static const uint16_t paint_palette[PAINT_COLORS] = {
    0x0000, 0x0010, 0x0400, 0x0410,
    0x8000, 0x8010, 0x8400, 0x8410,
    0x4208, 0x001F, 0x07E0, 0x07FF,
    0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

uint16_t paint_palette_rgb565(uint8_t index) {
    return paint_palette[index & 0x0F];
}

void paint_render_set_palette(void) {
    graphics_set_palette(paint_palette, PAINT_COLORS);
}

static void paint_draw_button(int index, const char *label, bool active, uint8_t color) {
    int x = index * PAINT_BUTTON_W;
    int w = PAINT_BUTTON_W;
    if (x + w > display_get_width()) {
        w = display_get_width() - x;
    }

    uint8_t bg = active ? 0 : color;
    uint8_t fg = active ? 15 : 0;

    graphics_fill_rect(x, 0, w, PAINT_TOP_BAR_H, bg);
    graphics_fill_rect(x + 1, 1, w - 2, PAINT_TOP_BAR_H - 2, bg);

    int text_x = x + (w - (int)strlen(label) * 6) / 2;
    int text_y = 3;
    graphics_draw_string(text_x, text_y, label, fg);
}

void paint_render_ui(const paint_state_t *state) {
    paint_draw_button(0, "PEN", state->tool == PAINT_TOOL_PENCIL, 1);
    paint_draw_button(1, "ERS", state->tool == PAINT_TOOL_ERASER, 9);
    paint_draw_button(2, "LIN", state->tool == PAINT_TOOL_LINE, 2);
    paint_draw_button(3, "REC", state->tool == PAINT_TOOL_RECT, 9);
    paint_draw_button(4, "CLR", false, 4);
    paint_draw_button(5, "UND", false, state->has_undo ? 10 : 8);
    paint_draw_button(6, "SAV", false, 3);
    paint_draw_button(7, "LOD", false, 12);
    paint_draw_button(8, "EXT", false, 12);

    paint_render_preview_line(state);

    int swatch_w = display_get_width() / PAINT_COLORS;
    int y = display_get_height() - PAINT_PALETTE_H;
    for (int color = 0; color < PAINT_COLORS; color++) {
        int x = color * swatch_w;
        int width = (color == PAINT_COLORS - 1) ? (display_get_width() - x) : swatch_w;
        graphics_fill_rect(x, y, width, PAINT_PALETTE_H, color);

        if ((uint8_t)color == state->current_color) {
            uint8_t border = (color == 0 || color == 8) ? 15 : 0;
            graphics_fill_rect(x, y, width, 1, border);
            graphics_fill_rect(x, y + PAINT_PALETTE_H - 1, width, 1, border);
            graphics_fill_rect(x, y, 1, PAINT_PALETTE_H, border);
            graphics_fill_rect(x + width - 1, y, 1, PAINT_PALETTE_H, border);
        }
    }

    if (state->status[0]) {
        graphics_fill_rect(0, PAINT_TOP_BAR_H, display_get_width(), 10, 0);
        graphics_draw_string(4, PAINT_TOP_BAR_H + 1, state->status, 15);
    }

    graphics_flush();
}

void paint_render_pixel(const paint_state_t *state, int x, int y) {
    if (x < 0 || x >= display_get_width() || y < 0 || y >= display_get_height()) {
        return;
    }

    uint8_t color = paint_canvas_get(state, x, y);
    graphics_draw_pixel(x, y, color);
}

static void draw_preview_line_on_graphics(int x0, int y0, int x1, int y1, uint8_t color) {
    graphics_draw_line(x0, y0, x1, y1, color);
}

void paint_render_preview_line(const paint_state_t *state) {
    if (state->preview_active && state->shape_pending) {
        if (state->tool == PAINT_TOOL_LINE) {
            draw_preview_line_on_graphics(state->shape_start_x, state->shape_start_y,
                                          state->preview_x, state->preview_y, 15);
        } else if (state->tool == PAINT_TOOL_RECT) {
            int left = (state->shape_start_x < state->preview_x) ? state->shape_start_x : state->preview_x;
            int right = (state->shape_start_x > state->preview_x) ? state->shape_start_x : state->preview_x;
            int top = (state->shape_start_y < state->preview_y) ? state->shape_start_y : state->preview_y;
            int bottom = (state->shape_start_y > state->preview_y) ? state->shape_start_y : state->preview_y;

            graphics_draw_rect(left, top, right - left + 1, bottom - top + 1, 15);
        }
    }
}

void paint_render_canvas(const paint_state_t *state) {
    graphics_flush();
}

void paint_render_all(const paint_state_t *state) {
    paint_render_ui(state);
}

void paint_render_status(paint_state_t *state, const char *message) {
    strncpy(state->status, message, sizeof(state->status) - 1);
    state->status[sizeof(state->status) - 1] = '\0';
    paint_render_ui(state);
}
