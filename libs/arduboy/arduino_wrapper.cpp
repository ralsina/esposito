#include "arduino_compat.h"
#include "arduboy.h"
#include "arduboy_tunes.h"
#include <os_core.h>
#include <text_mode.h>
#include <stdio.h>
#include <stdlib.h>

// Global state for Arduino wrapper integration
static app_context_t *g_app_ctx = NULL;

// The Numbers game defines these instances
extern Arduboy arduboy;
extern ArduboyTunes tunes;

// Arduino app lifecycle functions
void app_init(app_context_t *ctx) {
    printf("Arduino app_init: setting up Arduboy compatibility\n");

    // Store app context for later use
    g_app_ctx = ctx;
    arduboy.setAppContext(ctx);

    // Subscribe to timer and keyboard events
    ctx->subscriptions = EVENT_TIMER | EVENT_KEYBOARD | EVENT_TOUCH;

    // Set frame rate (default 60 FPS, will be overridden by game)
    ctx->timer_interval_ms = 16; // ~60 FPS

    // Initialize Arduboy system
    arduboy.begin();

    // Call the game's setup function
    printf("Arduino app_init: calling game setup()\n");
    setup();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (!event) return;

    switch (event->type) {
        case EVENT_TIMER:
            // Frame timer - update input and call game loop
            arduboy.updateInput();
            loop();
            break;

        case EVENT_KEYBOARD:
            // Handle keyboard input
            arduboy_handle_key_event(event->keyboard.key, event->keyboard.pressed);
            break;

        case EVENT_TOUCH:
            // Handle touch input (TODO: implement)
            break;

        default:
            break;
    }
}

void app_checkpoint(app_context_t *ctx) {
    // Save game state if needed
}

void app_close(app_context_t *ctx) {
    // Cleanup
    text_mode_clear(0x0000);
    text_mode_flush();
}