#include "os_core.h"
#include "checkpoint.h"
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

    state.canvas = (uint8_t *)malloc(PAINT_CANVAS_BYTES);
    state.undo = (uint8_t *)malloc(PAINT_CANVAS_BYTES);
    state.current_color = 15;
    state.tool = PAINT_TOOL_PENCIL;
    strncpy(state.project_path, "/sdcard/paint_last.pt16", sizeof(state.project_path) - 1);
    state.project_path[sizeof(state.project_path) - 1] = '\0';

    checkpoint_open("paint");

    const char *saved_path = checkpoint_load_string("project_path");
    if (saved_path && saved_path[0]) {
        strncpy(state.project_path, saved_path, sizeof(state.project_path) - 1);
        state.project_path[sizeof(state.project_path) - 1] = '\0';
    }

    if (!state.canvas || !state.undo) {
        if (state.canvas) {
            free(state.canvas);
            state.canvas = NULL;
        }
        if (state.undo) {
            free(state.undo);
            state.undo = NULL;
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

    checkpoint_save_string("project_path", state.project_path);
    checkpoint_save_int("tool", (int)state.tool);
    checkpoint_save_int("color", (int)state.current_color);
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

    checkpoint_close();
    display_clear(0x0000);
}
