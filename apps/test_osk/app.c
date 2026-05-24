#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "test_osk";

#define MAX_INPUT 64

typedef struct {
    char buffer[MAX_INPUT];
    bool done;
} test_state_t;

static test_state_t g_test_state;

void app_init(app_context_t *ctx) {
    os_log(TAG, "Test OSK app initializing");

    if (!text_mode_init()) {
        os_log(TAG, "Failed to init text mode");
        return;
    }

    // Subscribe to keyboard and touch events
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;

    // Display initial screen
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(0, 0, "OSK Test Application", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    text_mode_print_at(0, 2, "Press any key to start");
    text_mode_print_at(0, 4, "This will test the on-screen");
    text_mode_print_at(0, 5, "keyboard with 4x10 layout.");
    text_mode_print_at(0, 7, "Features:");
    text_mode_print_at(0, 8, "- Ortholinear 4x10 layout");
    text_mode_print_at(0, 9, "- Dynamic labels with shift");
    text_mode_print_at(0, 10, "- Enter/ESC to confirm/cancel");

    g_test_state.buffer[0] = '\0';
    g_test_state.done = false;

    os_log(TAG, "Test OSK app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    // No special state to save
}

void app_close(app_context_t *ctx) {
    os_log(TAG, "Test OSK app cleanup");
    text_mode_clear(TEXT_COLOR_BLACK);
}

void app_event(app_context_t *ctx, event_t *event) {
    // Check if OSK is active and handle its events first
    if (ui_osk_is_active()) {
        ui_osk_handle_event(ctx, event);

        // Check if OSK just completed
        if (!ui_osk_is_active()) {
            ui_osk_result_t result = ui_osk_get_result();

            // Display result
            text_mode_clear(TEXT_COLOR_BLACK);
            text_mode_print_at_attr(0, 0, "OSK Test Result", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);

            if (result == UI_OSK_RESULT_CONFIRMED) {
                text_mode_print_at(0, 2, "Input confirmed!");
                text_mode_printf_at(0, 4, "You entered: %s", g_test_state.buffer);
            } else {
                text_mode_print_at(0, 2, "Input cancelled!");
                text_mode_print_at(0, 4, "User pressed ESC");
            }

            text_mode_print_at(0, 6, "Press any key to exit");

            g_test_state.done = true;
        }
        return;
    }

    // Handle normal app events
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        if (g_test_state.done) {
            // Exit app
            return;
        } else {
            // Start OSK
            g_test_state.buffer[0] = '\0';
            g_test_state.done = false;

            if (ui_osk_input_text("Enter text:", g_test_state.buffer, MAX_INPUT, NULL, false)) {
                os_log(TAG, "Started OSK input");
            } else {
                os_log(TAG, "Failed to start OSK");
            }
        }
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        // Also handle touch to start OSK
        if (!g_test_state.done && !ui_osk_is_active()) {
            g_test_state.buffer[0] = '\0';
            g_test_state.done = false;

            if (ui_osk_input_text("Enter text:", g_test_state.buffer, MAX_INPUT, NULL, false)) {
                os_log(TAG, "Started OSK input via touch");
            } else {
                os_log(TAG, "Failed to start OSK");
            }
        }
    }
}
