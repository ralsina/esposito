#include "os_core.h"
#include "text_mode.h"
#include "terminal_mode.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "vt100_probe";
static terminal_mode_t *term = NULL;

static void draw_probe_grid(void) {
    if (!term) return;

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    if (cols < 1 || rows < 1) return;

    terminal_mode_init(term, cols, rows, NULL);
    terminal_mode_set_status(term, "VT100 probe: row 01..N patterns");

    char stream[256];
    int stream_len = 0;

    stream_len = snprintf(stream, sizeof(stream), "\x1b[2J\x1b[H");
    terminal_mode_process_bytes(term, stream, (size_t)stream_len);

    char line[161];
    if (cols > (int)sizeof(line) - 1) {
        cols = (int)sizeof(line) - 1;
    }

    for (int row = 1; row <= rows; row++) {
        char pattern[9];
        int row_num = row % 100;
        snprintf(pattern, sizeof(pattern), "%02d%02d%02d%02d", row_num, row_num, row_num, row_num);

        for (int col = 0; col < cols; col++) {
            line[col] = pattern[col % 8];
        }
        line[cols] = '\0';

        stream_len = snprintf(stream, sizeof(stream), "\x1b[%d;1H%s", row, line);
        terminal_mode_process_bytes(term, stream, (size_t)stream_len);
    }

    terminal_mode_process_bytes(term, "\x1b[H", 3);
    terminal_mode_render(term);
}

void app_init(app_context_t *ctx) {
    os_log(TAG, "VT100 grid probe init");

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    term = terminal_mode_default();
    draw_probe_grid();
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;
    if (!event) return;

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        draw_probe_grid();
    }
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    os_log(TAG, "VT100 grid probe close");
}
