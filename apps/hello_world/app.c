#include "os_core.h"
#include <stdio.h>

static int counter = 0;

void app_init(app_context_t *ctx) {
    // Restore saved state
    counter = checkpoint_load_int("counter");

    // Subscribe to events
    ctx->subscriptions = EVENT_TIMER | EVENT_TOUCH;
    ctx->timer_interval_ms = 1000; // 1 second timer

    printf("Hello World app initialized\n");
}

void app_checkpoint(app_context_t *ctx) {
    // Save state
    checkpoint_save_int("counter", counter);
    printf("Counter saved: %d\n", counter);
}

void app_close(app_context_t *ctx) {
    printf("Hello World app closing\n");
}

void app_event(app_context_t *ctx, event_t *event) {
    switch (event->type) {
        case EVENT_TIMER:
            counter++;
            printf("Timer tick: %d\n", counter);
            break;

        case EVENT_TOUCH:
            if (event->touch.pressed) {
                printf("Touch at: %d, %d\n", event->touch.x, event->touch.y);
                counter = 0; // Reset counter on touch
            }
            break;

        default:
            break;
    }
}
