#include "paint_render.h"

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

static void paint_draw_button(int index, const char *label, bool active, uint16_t color) {
    int x = index * PAINT_BUTTON_W;
    int w = PAINT_BUTTON_W;
    if (x + w > PAINT_WIDTH) {
        w = PAINT_WIDTH - x;
    }

    uint16_t bg = active ? 0xFFFF : color;
    uint16_t fg = active ? 0x0000 : 0xFFFF;

    display_fill_rect(x, 0, w, PAINT_TOP_BAR_H, bg);
    display_fill_rect(x + 1, 1, w - 2, PAINT_TOP_BAR_H - 2, bg);
    display_draw_text(x + 2, 5, label, fg);
}

void paint_render_ui(const paint_state_t *state) {
    paint_draw_button(0, "PEN", state->tool == PAINT_TOOL_PENCIL, 0x001F);
    paint_draw_button(1, "ERS", state->tool == PAINT_TOOL_ERASER, 0x0010);
    paint_draw_button(2, "LIN", state->tool == PAINT_TOOL_LINE, 0x0400);
    paint_draw_button(3, "REC", state->tool == PAINT_TOOL_RECT, 0x8010);
    paint_draw_button(4, "CLR", false, 0x8000);
    paint_draw_button(5, "UND", false, state->has_undo ? 0x07E0 : 0x4208);
    paint_draw_button(6, "SAV", false, 0x0410);
    paint_draw_button(7, "LOD", false, 0x8400);
    paint_draw_button(8, "EXT", false, 0xF800);

    paint_render_preview_line(state);

    int swatch_w = PAINT_WIDTH / PAINT_COLORS;
    int y = PAINT_HEIGHT - PAINT_PALETTE_H;
    for (int color = 0; color < PAINT_COLORS; color++) {
        int x = color * swatch_w;
        int width = (color == PAINT_COLORS - 1) ? (PAINT_WIDTH - x) : swatch_w;
        uint16_t rgb = paint_palette[color];
        display_fill_rect(x, y, width, PAINT_PALETTE_H, rgb);

        if ((uint8_t)color == state->current_color) {
            uint16_t border = (color == 0 || color == 8) ? 0xFFFF : 0x0000;
            display_fill_rect(x, y, width, 1, border);
            display_fill_rect(x, y + PAINT_PALETTE_H - 1, width, 1, border);
            display_fill_rect(x, y, 1, PAINT_PALETTE_H, border);
            display_fill_rect(x + width - 1, y, 1, PAINT_PALETTE_H, border);
        }
    }

    if (state->status[0]) {
        display_fill_rect(0, PAINT_TOP_BAR_H, PAINT_WIDTH, 10, 0x0000);
        display_draw_text(2, PAINT_TOP_BAR_H + 1, state->status, 0xFFFF);
    }
}

void paint_render_pixel(const paint_state_t *state, int x, int y) {
    if (x < 0 || x >= PAINT_WIDTH || y < 0 || y >= PAINT_HEIGHT) {
        return;
    }

    uint8_t color = paint_canvas_get(state, x, y);
    display_draw_pixel(x, y, paint_palette_rgb565(color));
}

static void draw_preview_line_on_display(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if (x0 >= 0 && x0 < PAINT_WIDTH && y0 >= 0 && y0 < PAINT_HEIGHT) {
            display_draw_pixel(x0, y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void paint_render_preview_line(const paint_state_t *state) {
    if (state->preview_active && state->shape_pending) {
        if (state->tool == PAINT_TOOL_LINE) {
            draw_preview_line_on_display(state->shape_start_x, state->shape_start_y,
                                         state->preview_x, state->preview_y, 0xFFFF);
        } else if (state->tool == PAINT_TOOL_RECT) {
            int left = (state->shape_start_x < state->preview_x) ? state->shape_start_x : state->preview_x;
            int right = (state->shape_start_x > state->preview_x) ? state->shape_start_x : state->preview_x;
            int top = (state->shape_start_y < state->preview_y) ? state->shape_start_y : state->preview_y;
            int bottom = (state->shape_start_y > state->preview_y) ? state->shape_start_y : state->preview_y;

            for (int x = left; x <= right; x++) {
                if (x >= 0 && x < PAINT_WIDTH) {
                    if (top >= 0 && top < PAINT_HEIGHT) {
                        display_draw_pixel(x, top, 0xFFFF);
                    }
                    if (bottom >= 0 && bottom < PAINT_HEIGHT) {
                        display_draw_pixel(x, bottom, 0xFFFF);
                    }
                }
            }
            for (int y = top; y <= bottom; y++) {
                if (y >= 0 && y < PAINT_HEIGHT) {
                    if (left >= 0 && left < PAINT_WIDTH) {
                        display_draw_pixel(left, y, 0xFFFF);
                    }
                    if (right >= 0 && right < PAINT_WIDTH) {
                        display_draw_pixel(right, y, 0xFFFF);
                    }
                }
            }
        }
    }
}

void paint_render_all(const paint_state_t *state) {
    for (int y = 0; y < PAINT_HEIGHT; y++) {
        for (int x = 0; x < PAINT_WIDTH; x++) {
            uint8_t color = paint_canvas_get(state, x, y);
            display_draw_pixel(x, y, paint_palette_rgb565(color));
        }
    }

    paint_render_ui(state);
}

void paint_render_status(paint_state_t *state, const char *message) {
    strncpy(state->status, message, sizeof(state->status) - 1);
    state->status[sizeof(state->status) - 1] = '\0';
    paint_render_ui(state);
}
