#include "os_core.h"
#include "hardware.h"
#include <stdio.h>

static int counter = 0;

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;
    display_clear(0x0000);
    display_draw_text(10, 10, "SD Card App Loaded!", 0x07E0);
    display_draw_text(10, 30, "Press keys to test", 0xFFFF);
}

void app_checkpoint(app_context_t *ctx) {
    checkpoint_save_int("counter", counter);
}

void app_close(app_context_t *ctx) {
    display_clear(0x0000);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        counter++;
        char msg[32];
        snprintf(msg, sizeof(msg), "Key: %c (%d)", event->keyboard.key, counter);
        display_draw_text(10, 60, msg, 0xFFE0);
    }
}
