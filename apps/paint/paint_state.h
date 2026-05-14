#ifndef PAINT_STATE_H
#define PAINT_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define PAINT_WIDTH 320
#define PAINT_HEIGHT 240
#define PAINT_COLORS 16
#define PAINT_CANVAS_BYTES ((PAINT_WIDTH * PAINT_HEIGHT) / 2)
#define PAINT_TOP_BAR_H 18
#define PAINT_PALETTE_H 20
#define PAINT_BUTTON_W 35
#define PAINT_BUTTON_COUNT 9
#define PAINT_PREVIEW_MAX_POINTS 1536

typedef enum {
    PAINT_TOOL_PENCIL = 0,
    PAINT_TOOL_ERASER,
    PAINT_TOOL_LINE,
    PAINT_TOOL_RECT,
} paint_tool_t;

typedef struct {
    uint8_t *canvas;
    uint8_t *undo;
    int16_t *preview_points_x;
    int16_t *preview_points_y;
    bool has_undo;
    bool touch_active;
    int stroke_last_x;
    int stroke_last_y;
    uint8_t current_color;
    paint_tool_t tool;
    bool shape_pending;
    int shape_start_x;
    int shape_start_y;
    bool preview_active;
    int preview_point_count;
    int preview_x;
    int preview_y;
    char project_path[128];
    char status[48];
} paint_state_t;

uint8_t paint_canvas_get(const paint_state_t *state, int x, int y);
void paint_canvas_set(paint_state_t *state, int x, int y, uint8_t color);
void paint_canvas_clear(paint_state_t *state, uint8_t color);
void paint_canvas_snapshot_undo(paint_state_t *state);
void paint_canvas_restore_undo(paint_state_t *state);

#endif
