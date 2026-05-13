#include "os_core.h"
#include "checkpoint.h"
#include "text_mode.h"
#include "reader_state.h"
#include "reader_core.h"
#include "reader_events.h"
extern void app_launcher_start(void);
#include <string.h>
#include <sys/stat.h>

static reader_state_t state;
static int bold_pending = 0;
static int underline_pending = 0;

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    ctx->timer_interval_ms = 0;

    memset(&state, 0, sizeof(state));
    text_mode_init();
    checkpoint_open("reader");

    // Try last-opened file restore (legacy fallback included).
    const char *saved_file = checkpoint_load_string(KEY_LAST_FILE);
    if (!saved_file || !saved_file[0]) {
        saved_file = checkpoint_load_string(KEY_LEGACY_LAST_FILE);
    }

    if (saved_file && saved_file[0]) {
        struct stat st;
        if (stat(saved_file, &st) == 0 && S_ISREG(st.st_mode)) {
            if (reader_events_open_book(&state, saved_file, &bold_pending, &underline_pending)) {
                reader_events_enter_reading_mode(&state, &bold_pending, &underline_pending);
                return;
            }
        }
    }

    reader_events_show_file_list(&state);
}

void app_event(app_context_t *ctx, event_t *event) {
    reader_events_handle_event(&state, event, &bold_pending, &underline_pending, app_launcher_start);
}

void app_checkpoint(app_context_t *ctx) {
    reader_save_current_book_progress(&state);
}

void app_close(app_context_t *ctx) {
    reader_close_current_file(&state);
    checkpoint_close();
    text_mode_clear(TEXT_COLOR_BLACK);
}
