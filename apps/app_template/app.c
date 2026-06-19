#include "os_core.h"
#include "text_mode.h"
#include <stdio.h>

static const char *TAG = "app_template";

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD;
    text_mode_init();
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(2, 2, "New App", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 4, "Press Ctrl+Esc to exit.", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    os_log(TAG, "initialized");
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        text_mode_printf_at_attr(2, 6, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_NORMAL,
                                 "Key: '%c' (0x%02X)",
                                 key > 32 && key < 127 ? key : '?', (unsigned char)key);
    }
}

void app_checkpoint(app_context_t *ctx) {
    os_log(TAG, "checkpoint");
}

void app_close(app_context_t *ctx) {
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "closed");
}
