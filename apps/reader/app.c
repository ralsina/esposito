#include "os_core.h"
#include "text_mode.h"
#include "app_config.h"
#include "reader_state.h"
#include "reader_core.h"
#include "reader_events.h"
#include "reader_startup.h"
#include "reader_view.h"
#include "reader_render_pipeline.h"
#include "serial_rx.h"
#include "hardware.h"
#include <string.h>

static reader_state_t state;
static int bold_pending = 0;
static int underline_pending = 0;

static void go_to_launcher(void) { os_exit(); }

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH | EVENT_SERIAL | EVENT_TIMER;
    ctx->timer_interval_ms = 100;

    memset(&state, 0, sizeof(state));
    state.screen = ui2_screen_create();
    reader_startup_init(&state, &bold_pending, &underline_pending);

    if (!config_bind_app("reader")) {
        os_log("reader", "Warning: could not bind reader config namespace");
    }
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_SERIAL) {
        if (state.mode == MODE_RECEIVING) {
            serial_rx_process_bytes(event->serial.data, event->serial.len);
        }
        return;
    }

    if (event->type == EVENT_TIMER) {
        if (state.mode == MODE_READING && state.pipeline.task) {
            render_pipeline_consume_done(&state.pipeline);
        }
        return;
    }

    reader_events_handle_event(&state, event, &bold_pending, &underline_pending, go_to_launcher);
}

void app_checkpoint(app_context_t *ctx) {
    reader_save_current_book_progress(&state, true);
}

void app_close(app_context_t *ctx) {
    render_pipeline_shutdown(&state.pipeline);
    state.lines = NULL;
    state.line_buf_size = 0;
    reader_close_current_file(&state);
    reader_free_file_list(&state);
    reader_free_toc_titles(&state);
    reader_free_lines(&state);

    if (state.screen) {
        ui2_screen_destroy(state.screen);
        state.screen = NULL;
    }

    config_unbind_app();
    text_mode_clear(TEXT_COLOR_BLACK);
}
