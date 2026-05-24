#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "test_button";

static ui_button_t *g_button = NULL;
static bool is_uppercase = true;

void app_init(app_context_t *ctx) {
    os_log(TAG, "Test button app initializing");

    if (!text_mode_init()) {
        os_log(TAG, "Failed to init text mode");
        return;
    }

    // Subscribe to touch events
    ctx->subscriptions = EVENT_TOUCH;

    // Clear screen
    text_mode_clear(TEXT_COLOR_BLACK);

    // Display title
    text_mode_print_at_attr(0, 0, "Button Test", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    text_mode_print_at(0, 2, "Press the button to");
    text_mode_print_at(0, 3, "change its label.");

    // Create a button labeled "A"
    g_button = ui_button_create(5, 6, 4, 2, "A");
    if (g_button) {
        ui_button_set_colors(g_button, TEXT_COLOR_WHITE, TEXT_COLOR_BLUE);
        ui_button_set_callback(g_button, NULL, NULL); // No callback needed
        ui_button_draw(g_button);
    }

    os_log(TAG, "Test button app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    // No special state to save
}

void app_close(app_context_t *ctx) {
    os_log(TAG, "Test button app cleanup");
    if (g_button) {
        ui_button_destroy(g_button);
        g_button = NULL;
    }
    text_mode_clear(TEXT_COLOR_BLACK);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_TOUCH && event->touch.pressed) {
        // Check if button was pressed
        if (g_button && ui_button_handle_touch(g_button, event)) {
            // Toggle between uppercase and lowercase
            const char *new_label = is_uppercase ? "a" : "A";

            os_log(TAG, "Button pressed! Changing label to: %s", new_label);

            // Update button label
            ui_button_set_text(g_button, new_label);
            ui_button_draw(g_button);

            // Toggle state
            is_uppercase = !is_uppercase;
        }
    }
}
