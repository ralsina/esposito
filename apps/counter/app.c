// Counter App - Simple counter with keyboard control
// Demonstrates app switching and state management

#include "os_core.h"
#include "hardware.h"
#include <stdio.h>
#include <string.h>
#include "esp_system.h"

static int counter = 0;
static int display_x = 10;
static int display_y = 10;

void counter_app_init(app_context_t *ctx) {
    printf("🔢 COUNTER APP_INIT called\n");
    printf("🔢 Initial subscriptions: 0x%lX\n", (unsigned long)ctx->subscriptions);

    // Subscribe to keyboard events for counter control and app switching
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    printf("🔢 Set subscriptions to 0x%lX (EVENT_KEYBOARD=0x%lX)\n",
           (unsigned long)ctx->subscriptions, (unsigned long)EVENT_KEYBOARD);

    // Initialize counter
    counter = 0;

    // Display app interface
    display_clear(0x0000); // Black background
    display_draw_text(5, 5, "Counter App v1.0", 0x07E0); // Green title
    display_draw_text(5, 25, "Keys: +/- to count", 0xFFFF); // White instructions
    display_draw_text(5, 45, "Q: quit to shell", 0xF800); // Red instructions

    // Display current counter value
    char buf[32];
    snprintf(buf, sizeof(buf), "Count: %d", counter);
    display_draw_text(display_x, display_y, buf, 0x07FF); // Cyan

    printf("🔢 Counter app initialized\n");
    printf("🔢 Counter: %d\n", counter);
    printf("🔢 Controls: +/- to change count, S to switch apps, Q to quit\n");
}

void counter_app_checkpoint(app_context_t *ctx) {
    printf("🔢 COUNTER CHECKPOINT (count: %d)\n", counter);
    // Could save counter to non-volatile storage here
}

void counter_app_close(app_context_t *ctx) {
    printf("🔢 COUNTER CLOSE\n");
    display_clear(0x0000); // Clear screen
}

void counter_app_event(app_context_t *ctx, event_t *event) {
    if (event->type != EVENT_KEYBOARD) {
        return;
    }

    if (event->keyboard.pressed) {
        char key = event->keyboard.key;

        switch (key) {
            case '+':
            case '=':
                // Increment counter
                counter++;
                printf("🔢 Counter incremented: %d\n", counter);
                break;

            case '-':
            case '_':
                // Decrement counter
                counter--;
                printf("🔢 Counter decremented: %d\n", counter);
                break;

            case 'q':
            case 'Q':
                // Quit to shell (for now, just reboot)
                printf("🔢 Quit requested, rebooting...\n");
                display_draw_text(5, 85, "Rebooting...", 0xF800);
                esp_restart();
                break;

            default:
                // Echo other keys
                if (key >= 32 && key <= 126) {
                    printf("🔢 Key: %c (0x%02X)\n", key, (unsigned char)key);
                }
                break;
        }

        // Update display with current counter value
        char buf[32];
        snprintf(buf, sizeof(buf), "Count: %d", counter);
        display_draw_text(display_x, display_y, buf, 0x07FF); // Cyan
    }
}