#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "os_core.h"
#include "text_mode.h"
#include "app_heap.h"

#ifdef __cplusplus
extern "C" {
#endif

// Button widget - handles drawing and touch detection automatically
typedef struct ui_button {
    int x, y;              // Position
    int width, height;     // Dimensions
    char *text;            // Button label
    uint16_t fg_color;     // Text color
    uint16_t bg_color;     // Background color
    bool visible;          // Whether to draw
    bool enabled;          // Whether to respond to events

    // Callback for button press
    void (*on_click)(struct ui_button *button, void *user_data);
    void *user_data;       // User data passed to callback
} ui_button_t;

// Create a button widget
ui_button_t* ui_button_create(int x, int y, int width, int height, const char *text);

// Destroy a button widget
void ui_button_destroy(ui_button_t *button);

// Draw a button
void ui_button_draw(ui_button_t *button);

// Check if a touch event hits this button
bool ui_button_handle_touch(ui_button_t *button, const event_t *event);

// Set button callback
void ui_button_set_callback(ui_button_t *button, void (*on_click)(ui_button_t *button, void *user_data), void *user_data);

// Set button colors
void ui_button_set_colors(ui_button_t *button, uint16_t fg, uint16_t bg);

// Enable/disable button
void ui_button_set_enabled(ui_button_t *button, bool enabled);

// Show/hide button
void ui_button_set_visible(ui_button_t *button, bool visible);

#ifdef __cplusplus
}
#endif

#endif // UI_BUTTON_H