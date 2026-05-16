#include "os_core.h"
#include "text_mode.h"
#include "app_config.h"
#include "reader_state.h"
#include "reader_core.h"
#include "reader_events.h"
#include "reader_startup.h"
extern void app_launcher_start(void);
#include <string.h>

static reader_state_t state;
static int bold_pending = 0;
static int underline_pending = 0;

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    ctx->timer_interval_ms = 0;

    memset(&state, 0, sizeof(state));
    reader_startup_init(&state, &bold_pending, &underline_pending);

    // Keep reader config namespace bound during runtime for progress save/load.
    if (!config_bind_app("reader")) {
        os_log("reader", "Warning: could not bind reader config namespace");
    }
}

void app_event(app_context_t *ctx, event_t *event) {
    reader_events_handle_event(&state, event, &bold_pending, &underline_pending, app_launcher_start);
}

void app_checkpoint(app_context_t *ctx) {
    reader_save_current_book_progress(&state);
}

void app_close(app_context_t *ctx) {
    reader_close_current_file(&state);
    reader_free_file_list(&state);
    reader_free_toc_titles(&state);

    // Clean up widgets
    if (state.search_widget) {
        ui_text_input_destroy(state.search_widget);
        state.search_widget = NULL;
    }
    if (state.goto_widget) {
        ui_text_input_destroy(state.goto_widget);
        state.goto_widget = NULL;
    }

    config_unbind_app();
    text_mode_clear(TEXT_COLOR_BLACK);
}
