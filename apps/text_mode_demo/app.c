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

    // Display welcome screen with rich text attributes
    text_mode_clear(TEXT_COLOR_BLACK);

    // Draw title with bold + bright cyan
    text_mode_print_at_attr((TEXT_MODE_COLS - 20) / 2, 2, "TEXT MODE DEMO",
                              TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    // Show all 16 colors in a grid
    text_mode_print_at_attr(5, 5, "16-Color Palette:", TEXT_COLOR_WHITE, TEXT_ATTR_BOLD);

    // Normal colors (0-7)
    for (int i = 0; i < 8; i++) {
        char label[16];
        snprintf(label, sizeof(label), "Color%02d", i);
        text_mode_print_at_attr(5 + (i % 4) * 10, 7 + (i / 4) * 2, label, i, TEXT_ATTR_NORMAL);
    }

    // Bright colors (8-15)
    for (int i = 0; i < 8; i++) {
        char label[16];
        snprintf(label, sizeof(label), "Br%02d", i + 8);
        text_mode_print_at_attr(5 + (i % 4) * 10, 11 + (i / 4) * 2, label, i + 8, TEXT_ATTR_NORMAL);
    }

    // Demonstrate text attributes
    text_mode_print_at_attr(5, 16, "Text Attributes:", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);

    text_mode_print_at_attr(5, 17, "Normal text", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(5, 18, "Bold text", TEXT_COLOR_GREEN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(5, 19, "Underlined text", TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    text_mode_print_at_attr(5, 20, "Inverse text", TEXT_COLOR_RED, TEXT_ATTR_INVERSE);
    text_mode_print_at_attr(5, 21, "Bold + Underline", TEXT_COLOR_MAGENTA, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);

    // Instructions
    text_mode_print_at_attr(5, 24, "Press keys to echo (try modifiers!)", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    text_mode_printf_at_attr(5, 25, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_NORMAL, "Counter: %d", counter);

    // Test combining attributes
    text_mode_print_at_attr(5, 27, "Combined:", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);
    text_mode_print_at_attr(5, 28, "Bold+Under+Inverse!", TEXT_COLOR_BRIGHT_RED, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE | TEXT_ATTR_INVERSE);

    counter = 0;
    ESP_LOGI(TAG, "Text Mode Demo initialized with rich text attributes");
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
            text_mode_printf_at_attr(14, 25, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD, "%d", counter);

            // Display the key that was pressed with styling
            uint8_t display_color = TEXT_COLOR_BRIGHT_GREEN;
            uint8_t display_attrs = TEXT_ATTR_NORMAL;

            // Add styling based on key value to demonstrate attributes
            if (counter % 4 == 0) {
                display_attrs |= TEXT_ATTR_BOLD;
            }
            if (counter % 4 == 1) {
                display_attrs |= TEXT_ATTR_UNDERLINE;
            }
            if (counter % 4 == 2) {
                display_attrs |= TEXT_ATTR_INVERSE;
            }

            // Show modifiers if any
            if (event->keyboard.modifiers) {
                text_mode_printf_at_attr(5, 26, TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD,
                                        "Key: %c (0x%02X) Mods:0x%02X",
                                        key > 32 && key < 127 ? key : '?', key, event->keyboard.modifiers);
            } else {
                text_mode_printf_at_attr(5, 26, display_color, display_attrs,
                                        "Key: '%c' (0x%02X)",
                                        key > 32 && key < 127 ? key : '?', key);
            }

            ESP_LOGI(TAG, "Key pressed: %c (0x%02X), counter: %d, mods: 0x%02X",
                     key > 32 && key < 127 ? key : '?', key, counter, event->keyboard.modifiers);
        }
    }
}