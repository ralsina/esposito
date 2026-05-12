// Esposito Example App
// Minimal app demonstrating the four required entry points,
// keyboard input, checkpoint save/restore, and display output.
// Copy this directory and rename things to create a new app.

#include "os_core.h"
#include "hardware.h"
#include "checkpoint.h"
#include <stdio.h>
#include <string.h>

// App state — saved/restored via checkpoint API
static int counter = 0;
static char last_key[32] = "";

// --- The four entry points ---

// Called once when the app is loaded. Restore state, subscribe to events.
void app_init(app_context_t *ctx) {
    counter = checkpoint_load_int("counter");
    const char *saved = checkpoint_load_string("last_key");
    if (saved) strcpy(last_key, saved);

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    display_clear(0x0000);
    display_draw_text(10, 10, "Example App v1", 0x07E0);
    display_draw_text(10, 60, "Press keys to begin", 0xFFE0);

    char msg[64];
    snprintf(msg, sizeof(msg), "Key: '%s' Count: %d",
             last_key[0] ? last_key : "none", counter);
    display_draw_text(10, 90, msg, 0xFFFF);
}

// Called before the app is unloaded. Save everything needed to restore above.
void app_checkpoint(app_context_t *ctx) {
    checkpoint_save_int("counter", counter);
    checkpoint_save_string("last_key", last_key);
}

// Called when the app is unloaded. Clean up.
void app_close(app_context_t *ctx) {
    display_clear(0x0000);
}

// Called for each event you subscribed to (keyboard, touch, timer, serial).
void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        counter++;

        snprintf(last_key, sizeof(last_key), "%c", key >= 32 ? key : '?');

        char msg[64];
        snprintf(msg, sizeof(msg), "Key: '%c' (0x%02X) Count: %d",
                 key >= 32 ? key : '?', (unsigned char)key, counter);
        display_draw_text(10, 90, "                              ", 0x0000);
        display_draw_text(10, 90, msg, 0xFFE0);

        // Persist state immediately so it survives crashes or power loss
        app_checkpoint(ctx);
        checkpoint_save();
    }
}
