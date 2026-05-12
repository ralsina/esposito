// Text Mode Demo
// Demonstrates the text mode API with grid-based positioning

#include "os_core.h"
#include "text_mode.h"
#include "hardware.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "text_mode_demo";

// App state
static int counter = 0;

void text_mode_demo_app_init(app_context_t *ctx) {
    ESP_LOGI(TAG, "Text Mode Demo initializing");

    // Initialize text mode
    if (!text_mode_init()) {
        ESP_LOGE(TAG, "Failed to initialize text mode");
        return;
    }

    // Subscribe to keyboard events
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    // Display welcome screen
    text_mode_clear(TEXT_COLOR_BLACK);

    // Draw a border using asterisks
    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, 0, "*", TEXT_COLOR_YELLOW);
        text_mode_print_at_color(x, TEXT_MODE_ROWS - 1, "*", TEXT_COLOR_YELLOW);
    }
    for (int y = 0; y < TEXT_MODE_ROWS; y++) {
        text_mode_print_at_color(0, y, "*", TEXT_COLOR_YELLOW);
        text_mode_print_at_color(TEXT_MODE_COLS - 1, y, "*", TEXT_COLOR_YELLOW);
    }

    // Title
    text_mode_printf_at_color((TEXT_MODE_COLS - 20) / 2, 2, TEXT_COLOR_GREEN,
                              "TEXT MODE DEMO");

    // Instructions
    text_mode_print_at_color(5, 5, "This demonstrates the", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, 6, "text mode API with", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, 7, "grid positioning.", TEXT_COLOR_WHITE);

    text_mode_print_at_color(5, 10, "Features:", TEXT_COLOR_CYAN);
    text_mode_print_at_color(5, 11, "  40x30 char grid", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, 12, "  Color support", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, 13, "  Cursor tracking", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, 14, "  Checkpoint save", TEXT_COLOR_WHITE);

    text_mode_print_at_color(5, 17, "Colors available:", TEXT_COLOR_MAGENTA);
    text_mode_print_at_color(5, 18, "BLACK WHITE RED", TEXT_COLOR_WHITE);
    text_mode_print_at_color(5, 19, "GREEN BLUE YELLOW", TEXT_COLOR_WHITE);

    // Color demonstration
    text_mode_print_at_color(5, 21, "Color demo:", TEXT_COLOR_RED);
    text_mode_print_at_color(5, 22, "RED", TEXT_COLOR_RED);
    text_mode_print_at_color(10, 22, "GREEN", TEXT_COLOR_GREEN);
    text_mode_print_at_color(17, 22, "BLUE", TEXT_COLOR_BLUE);
    text_mode_print_at_color(23, 22, "YELLOW", TEXT_COLOR_YELLOW);

    text_mode_print_at_color(5, 24, "Press keys to test", TEXT_COLOR_WHITE);
    text_mode_printf_at_color(5, 25, TEXT_COLOR_CYAN, "Counter: %d", counter);

    counter = 0;
    ESP_LOGI(TAG, "Text Mode Demo initialized");
}

void text_mode_demo_app_checkpoint(app_context_t *ctx) {
    // Save counter state
    checkpoint_save_int("counter", counter);

    // Let text mode save its state
    text_mode_save();

    ESP_LOGI(TAG, "Text Mode Demo state saved");
}

void text_mode_demo_app_close(app_context_t *ctx) {
    ESP_LOGI(TAG, "Text Mode Demo cleanup");
    text_mode_clear(TEXT_COLOR_BLACK);
}

void text_mode_demo_app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD) {
        if (event->keyboard.pressed) {
            char key = event->keyboard.key;

            // Update counter on any key press
            counter++;
            text_mode_printf_at_color(14, 25, TEXT_COLOR_CYAN, "%d", counter);

            // Display the key that was pressed
            text_mode_printf_at_color(5, 26, TEXT_COLOR_GREEN, "Key: '%c' (0x%02X)",
                                      key > 32 && key < 127 ? key : '?', key);

            ESP_LOGI(TAG, "Key pressed: %c (0x%02X), counter: %d",
                     key > 32 && key < 127 ? key : '?', key, counter);
        }
    }
}