#ifndef UI_TEXT_INPUT_H
#define UI_TEXT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "os_core.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct ui_text_input_widget_t ui_text_input_widget_t;

// Text input widget for keyboard-based text entry
// Ideal for search boxes, form fields, prompts, etc.
struct ui_text_input_widget_t {
    // Position and dimensions (in character cells)
    int x;
    int y;
    int width;
    int height;

    // Content
    char *buffer;           // Text buffer (owned by caller)
    int max_len;           // Maximum buffer length

    // Appearance
    char *title;           // Title text (owned by widget)
    char *label;           // Label text (owned by widget)
    bool mask_input;       // If true, show '*' instead of actual text

    // Hints
    char *hint_left;       // Left hint text (owned by widget)
    char *hint_right;      // Right hint text (owned by widget)

    // Colors
    uint8_t title_fg;
    uint8_t title_bg;
    uint8_t label_fg;
    uint8_t label_bg;
    uint8_t text_fg;
    uint8_t text_bg;
    uint8_t hint_fg;
    uint8_t hint_bg;

    // State
    bool visible;
    bool enabled;
    bool focused;

    // Callback
    void (*on_text_changed)(ui_text_input_widget_t *widget);
    void (*on_confirm)(ui_text_input_widget_t *widget);
    void (*on_cancel)(ui_text_input_widget_t *widget);
    void *user_data;
};

// Create and destroy text input widgets
ui_text_input_widget_t* ui_text_input_create(int x, int y, int width, int height);
void ui_text_input_destroy(ui_text_input_widget_t *widget);

// Configure text input widget
void ui_text_input_set_buffer(ui_text_input_widget_t *widget, char *buffer, int max_len);
void ui_text_input_set_title(ui_text_input_widget_t *widget, const char *title);
void ui_text_input_set_label(ui_text_input_widget_t *widget, const char *label);
void ui_text_input_set_hints(ui_text_input_widget_t *widget, const char *hint_left, const char *hint_right);
void ui_text_input_set_mask(ui_text_input_widget_t *widget, bool mask_input);
void ui_text_input_set_colors(ui_text_input_widget_t *widget,
                              uint8_t title_fg, uint8_t title_bg,
                              uint8_t label_fg, uint8_t label_bg,
                              uint8_t text_fg, uint8_t text_bg,
                              uint8_t hint_fg, uint8_t hint_bg);
void ui_text_input_set_callbacks(ui_text_input_widget_t *widget,
                                 void (*on_text_changed)(ui_text_input_widget_t *widget),
                                 void (*on_confirm)(ui_text_input_widget_t *widget),
                                 void (*on_cancel)(ui_text_input_widget_t *widget),
                                 void *user_data);

// Text input operations
void ui_text_input_set_focus(ui_text_input_widget_t *widget, bool focused);
void ui_text_input_clear(ui_text_input_widget_t *widget);

// Drawing and input
void ui_text_input_draw(const ui_text_input_widget_t *widget);
bool ui_text_input_handle_key(ui_text_input_widget_t *widget, char key);

#ifdef __cplusplus
}
#endif

#endif // UI_TEXT_INPUT_H