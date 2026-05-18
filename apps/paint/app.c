#include "os_core.h"
#include "app_config.h"
#include "hardware.h"
#include "paint_state.h"
#include "paint_render.h"
#include "paint_storage.h"
#include "paint_tools.h"

#include <stdlib.h>
#include <string.h>

extern void app_launcher_start(void);

static paint_state_t state;

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_TOUCH | EVENT_TOUCH_CONTINUOUS;
    ctx->timer_interval_ms = 0;

    memset(&state, 0, sizeof(state));

    int canvas_bytes = paint_get_canvas_bytes();
    state.canvas = (uint8_t *)malloc(canvas_bytes);
    state.undo = (uint8_t *)malloc(canvas_bytes);
    state.preview_points_x = (int16_t *)malloc(sizeof(int16_t) * PAINT_PREVIEW_MAX_POINTS);
    state.preview_points_y = (int16_t *)malloc(sizeof(int16_t) * PAINT_PREVIEW_MAX_POINTS);
    state.current_color = 15;
    state.tool = PAINT_TOOL_PENCIL;
    strncpy(state.project_path, "/sdcard/paint_last.pt16", sizeof(state.project_path) - 1);
    state.project_path[sizeof(state.project_path) - 1] = '\0';

    char saved_path[sizeof(state.project_path)];
    size_t saved_len = config_get_string("project_path", "", saved_path, sizeof(saved_path));
    if (saved_len > 0 && saved_path[0]) {
        strncpy(state.project_path, saved_path, sizeof(state.project_path) - 1);
        state.project_path[sizeof(state.project_path) - 1] = '\0';
    }

    if (!state.canvas || !state.undo || !state.preview_points_x || !state.preview_points_y) {
        if (state.canvas) {
            free(state.canvas);
            state.canvas = NULL;
        }
        if (state.undo) {
            free(state.undo);
            state.undo = NULL;
        }
        if (state.preview_points_x) {
            free(state.preview_points_x);
            state.preview_points_x = NULL;
        }
        if (state.preview_points_y) {
            free(state.preview_points_y);
            state.preview_points_y = NULL;
        }
        display_clear(0x0000);
        display_draw_text(4, 4, "Paint app: out of memory", 0xF800);
        display_draw_text(4, 20, "Press Ctrl+ESC", 0xFFFF);
        return;
    }

    if (!paint_storage_load(&state, state.project_path)) {
        paint_canvas_clear(&state, 0);
        strncpy(state.status, "New canvas", sizeof(state.status) - 1);
        state.status[sizeof(state.status) - 1] = '\0';
    } else {
        strncpy(state.status, "Loaded project", sizeof(state.status) - 1);
        state.status[sizeof(state.status) - 1] = '\0';
    }

    paint_render_all(&state);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (!state.canvas || !state.undo) {
        return;
    }

    if (event->type == EVENT_TOUCH || event->type == EVENT_TOUCH_CONTINUOUS) {
        paint_tools_handle_touch(&state, event->touch.x, event->touch.y, event->touch.pressed, app_launcher_start);
    }
}

void app_checkpoint(app_context_t *ctx) {
    if (!state.canvas || !state.undo) {
        return;
    }

    config_set_string("project_path", state.project_path);
    config_set_int("tool", (int)state.tool);
    config_set_int("color", (int)state.current_color);
    paint_storage_save(&state, state.project_path);
}

void app_close(app_context_t *ctx) {
    if (state.canvas && state.undo) {
        paint_storage_save(&state, state.project_path);
    }

    if (state.canvas) {
        free(state.canvas);
        state.canvas = NULL;
    }
    if (state.undo) {
        free(state.undo);
        state.undo = NULL;
    }
    if (state.preview_points_x) {
        free(state.preview_points_x);
        state.preview_points_x = NULL;
    }
    if (state.preview_points_y) {
        free(state.preview_points_y);
        state.preview_points_y = NULL;
    }

    display_clear(0x0000);
}
