#include "paint_tools.h"

#include "paint_render.h"
#include "paint_storage.h"

#include <string.h>

uint8_t paint_canvas_get(const paint_state_t *state, int x, int y) {
    if (!state || !state->canvas || x < 0 || x >= PAINT_WIDTH || y < 0 || y >= PAINT_HEIGHT) {
        return 0;
    }

    int index = y * PAINT_WIDTH + x;
    int byte_index = index >> 1;
    uint8_t value = state->canvas[byte_index];
    if ((index & 1) == 0) {
        return (value >> 4) & 0x0F;
    }
    return value & 0x0F;
}

void paint_canvas_set(paint_state_t *state, int x, int y, uint8_t color) {
    if (!state || !state->canvas || x < 0 || x >= PAINT_WIDTH || y < 0 || y >= PAINT_HEIGHT) {
        return;
    }

    int index = y * PAINT_WIDTH + x;
    int byte_index = index >> 1;
    uint8_t value = state->canvas[byte_index];
    color &= 0x0F;

    if ((index & 1) == 0) {
        value = (uint8_t)((value & 0x0F) | (color << 4));
    } else {
        value = (uint8_t)((value & 0xF0) | color);
    }

    state->canvas[byte_index] = value;
}

void paint_canvas_clear(paint_state_t *state, uint8_t color) {
    if (!state || !state->canvas) {
        return;
    }

    color &= 0x0F;
    memset(state->canvas, (uint8_t)((color << 4) | color), PAINT_CANVAS_BYTES);
}

void paint_canvas_snapshot_undo(paint_state_t *state) {
    if (!state || !state->canvas || !state->undo) {
        return;
    }
    memcpy(state->undo, state->canvas, PAINT_CANVAS_BYTES);
    state->has_undo = true;
}

void paint_canvas_restore_undo(paint_state_t *state) {
    if (!state || !state->canvas || !state->undo || !state->has_undo) {
        return;
    }
    memcpy(state->canvas, state->undo, PAINT_CANVAS_BYTES);
    state->has_undo = false;
}

static void draw_line_on_canvas(paint_state_t *state, int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        paint_canvas_set(state, x0, y0, color);
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

static void draw_rect_on_canvas(paint_state_t *state, int x0, int y0, int x1, int y1, uint8_t color) {
    int left = (x0 < x1) ? x0 : x1;
    int right = (x0 > x1) ? x0 : x1;
    int top = (y0 < y1) ? y0 : y1;
    int bottom = (y0 > y1) ? y0 : y1;

    for (int x = left; x <= right; x++) {
        paint_canvas_set(state, x, top, color);
        paint_canvas_set(state, x, bottom, color);
    }
    for (int y = top; y <= bottom; y++) {
        paint_canvas_set(state, left, y, color);
        paint_canvas_set(state, right, y, color);
    }
}

static void handle_toolbar_touch(paint_state_t *state, int x, void (*launch_app_list)(void)) {
    int button = x / PAINT_BUTTON_W;
    if (button < 0) {
        return;
    }

    switch (button) {
        case 0:
            state->tool = PAINT_TOOL_PENCIL;
            paint_render_status(state, "Tool: pencil");
            break;
        case 1:
            state->tool = PAINT_TOOL_ERASER;
            paint_render_status(state, "Tool: eraser");
            break;
        case 2:
            state->tool = PAINT_TOOL_LINE;
            state->shape_pending = false;
            paint_render_status(state, "Tool: line");
            break;
        case 3:
            state->tool = PAINT_TOOL_RECT;
            state->shape_pending = false;
            paint_render_status(state, "Tool: rect");
            break;
        case 4:
            paint_canvas_snapshot_undo(state);
            paint_canvas_clear(state, 0);
            state->shape_pending = false;
            paint_render_all(state);
            paint_render_status(state, "Canvas cleared");
            break;
        case 5:
            if (state->has_undo) {
                paint_canvas_restore_undo(state);
                state->shape_pending = false;
                paint_render_all(state);
                paint_render_status(state, "Undo");
            } else {
                paint_render_status(state, "Undo empty");
            }
            break;
        case 6:
            if (paint_storage_save(state, state->project_path)) {
                paint_render_status(state, "Saved");
            } else {
                paint_render_status(state, "Save failed");
            }
            break;
        case 7:
            if (paint_storage_load(state, state->project_path)) {
                state->shape_pending = false;
                paint_render_all(state);
                paint_render_status(state, "Loaded");
            } else {
                paint_render_status(state, "Load failed");
            }
            break;
        case 8:
            if (launch_app_list) {
                launch_app_list();
            }
            break;
        default:
            break;
    }
}

static void handle_canvas_touch(paint_state_t *state, int x, int y, bool pressed) {
    if (x < 0 || x >= PAINT_WIDTH || y < 0 || y >= PAINT_HEIGHT) {
        if (!pressed) {
            state->touch_active = false;
        }
        return;
    }

    if (state->tool == PAINT_TOOL_PENCIL || state->tool == PAINT_TOOL_ERASER) {
        if (pressed && !state->touch_active) {
            paint_canvas_snapshot_undo(state);
            state->touch_active = true;
            state->stroke_last_x = x;
            state->stroke_last_y = y;
        }
        if (!pressed) {
            state->touch_active = false;
            return;
        }
        uint8_t color = (state->tool == PAINT_TOOL_ERASER) ? 0 : state->current_color;
        draw_line_on_canvas(state, state->stroke_last_x, state->stroke_last_y, x, y, color);
        int dx = (x > state->stroke_last_x) ? (x - state->stroke_last_x) : (state->stroke_last_x - x);
        int sx = (state->stroke_last_x < x) ? 1 : -1;
        int dy = -((y > state->stroke_last_y) ? (y - state->stroke_last_y) : (state->stroke_last_y - y));
        int sy = (state->stroke_last_y < y) ? 1 : -1;
        int err = dx + dy;
        int px = state->stroke_last_x;
        int py = state->stroke_last_y;

        for (;;) {
            paint_render_pixel(state, px, py);
            if (px == x && py == y) {
                break;
            }
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                px += sx;
            }
            if (e2 <= dx) {
                err += dx;
                py += sy;
            }
        }

        state->stroke_last_x = x;
        state->stroke_last_y = y;
        paint_render_ui(state);
        return;
    }

    if (!pressed) {
        state->touch_active = false;
        state->preview_active = false;
        return;
    }

    if (state->shape_pending) {
        if (x >= 0 && x < PAINT_WIDTH && y >= 0 && y < PAINT_HEIGHT) {
            state->preview_x = x;
            state->preview_y = y;
            state->preview_active = true;
            paint_render_all(state);
        }
        if (state->touch_active) {
            return;
        }
        state->touch_active = true;
        if (state->tool == PAINT_TOOL_LINE) {
            draw_line_on_canvas(state, state->shape_start_x, state->shape_start_y, x, y, state->current_color);
            paint_render_all(state);
            paint_render_status(state, "Line drawn");
        } else if (state->tool == PAINT_TOOL_RECT) {
            draw_rect_on_canvas(state, state->shape_start_x, state->shape_start_y, x, y, state->current_color);
            paint_render_all(state);
            paint_render_status(state, "Rectangle drawn");
        }
        state->shape_pending = false;
        state->preview_active = false;
        return;
    }

    if (state->touch_active) {
        return;
    }
    state->touch_active = true;

    paint_canvas_snapshot_undo(state);
    state->shape_pending = true;
    state->shape_start_x = x;
    state->shape_start_y = y;
    state->preview_active = false;
    paint_render_status(state, "Shape start selected");
}

void paint_tools_handle_touch(paint_state_t *state, int x, int y, bool pressed, void (*launch_app_list)(void)) {
    if (!state) {
        return;
    }

    if (!pressed) {
        handle_canvas_touch(state, x, y, false);
        return;
    }

    if (y < PAINT_TOP_BAR_H) {
        handle_toolbar_touch(state, x, launch_app_list);
        return;
    }

    if (y >= PAINT_HEIGHT - PAINT_PALETTE_H) {
        int swatch_w = PAINT_WIDTH / PAINT_COLORS;
        int selected = x / swatch_w;
        if (selected < 0) {
            selected = 0;
        }
        if (selected >= PAINT_COLORS) {
            selected = PAINT_COLORS - 1;
        }
        state->current_color = (uint8_t)selected;
        paint_render_status(state, "Color selected");
        return;
    }

    handle_canvas_touch(state, x, y, true);
}
