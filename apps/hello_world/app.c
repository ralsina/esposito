#include "os_core.h"
#include "text_mode.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "hello_world";
static int counter = 0;

void app_init(app_context_t *ctx) {
    counter = checkpoint_load_int("counter");

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    text_mode_init();
    text_mode_clear(TEXT_COLOR_BLUE);

    text_mode_print_at_attr((TEXT_MODE_COLS - 12) / 2, 3, "Hello World!", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);

    text_mode_print_at_attr(5, 7, "This is a demo app", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(5, 8, "showing the text mode API.", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr(5, 10, "Checkpoint counter:", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    text_mode_printf_at_attr(5, 11, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_NORMAL, "%d", counter);

    text_mode_print_at_attr(5, 14, "Press any key to increment.", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(5, 15, "Press Ctrl+ESC for launcher.", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    os_log(TAG, "Hello World app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    checkpoint_save_int("counter", counter);
    os_log(TAG, "Counter saved: %d", counter);
}

void app_close(app_context_t *ctx) {
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "Hello World app closing");
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        counter++;
        text_mode_printf_at_attr(5, 11, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_NORMAL, "%d", counter);

        char key = event->keyboard.key;
        text_mode_printf_at_attr(5, 17, TEXT_COLOR_BRIGHT_MAGENTA, TEXT_ATTR_NORMAL,
                                "Last key: '%c' (0x%02X)",
                                key > 32 && key < 127 ? key : '?', (unsigned char)key);
        os_log(TAG, "Key pressed: %c (0x%02X), counter: %d",
               key > 32 && key < 127 ? key : '?', (unsigned char)key, counter);
    }
}
