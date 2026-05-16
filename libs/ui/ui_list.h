#ifndef UI_LIST_H
#define UI_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "os_core.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct ui_list_widget_t ui_list_widget_t;

// List widget for displaying scrollable, selectable item lists
// Ideal for TOCs, file pickers, settings menus, etc.
struct ui_list_widget_t {
    const char **items;      // Array of string pointers (owned by caller)
    int count;               // Number of items in the list
    int selected;            // Currently selected item index (0-based)
    int scroll_offset;       // Which item is at the top of the display
    int visible_rows;        // How many items can be displayed at once

    // Position and dimensions (in character cells)
    int x;
    int y;
    int width;
    int height;

    // Title
    char *title;            // Title text (owned by widget)

    // Colors
    uint8_t normal_fg;       // Normal item text color
    uint8_t normal_bg;       // Normal item background
    uint8_t selected_fg;     // Selected item text color
    uint8_t selected_bg;     // Selected item background
    uint8_t border_fg;       // Border color
    uint8_t title_fg;        // Title text color
    uint8_t title_bg;        // Title background color

    // State
    bool visible;
    bool enabled;
    bool draw_border;        // Draw a border around the list
    bool draw_scrollbar;     // Draw a scrollbar indicator

    // Callbacks
    void (*on_selection_changed)(ui_list_widget_t *widget, int new_selection);
    void (*on_item_selected)(ui_list_widget_t *widget, int item_index);
    void *user_data;
};

// Create and destroy list widgets
ui_list_widget_t* ui_list_create(int x, int y, int width, int height);
void ui_list_destroy(ui_list_widget_t *widget);

// Configure list widget
void ui_list_set_items(ui_list_widget_t *widget, const char **items, int count);
void ui_list_set_title(ui_list_widget_t *widget, const char *title);
void ui_list_set_colors(ui_list_widget_t *widget, uint8_t normal_fg, uint8_t normal_bg,
                        uint8_t selected_fg, uint8_t selected_bg, uint8_t border_fg);
void ui_list_set_callbacks(ui_list_widget_t *widget,
                           void (*on_selection_changed)(ui_list_widget_t *widget, int new_selection),
                           void (*on_item_selected)(ui_list_widget_t *widget, int item_index),
                           void *user_data);
void ui_list_set_border(ui_list_widget_t *widget, bool draw_border);
void ui_list_set_scrollbar(ui_list_widget_t *widget, bool draw_scrollbar);

// List operations
void ui_list_set_selection(ui_list_widget_t *widget, int index);
int ui_list_get_selection(const ui_list_widget_t *widget);
void ui_list_scroll_to_item(ui_list_widget_t *widget, int index);

// Drawing and input
void ui_list_draw(const ui_list_widget_t *widget);
bool ui_list_handle_key(ui_list_widget_t *widget, char key);
bool ui_list_handle_touch(ui_list_widget_t *widget, const event_t *event);

#ifdef __cplusplus
}
#endif

#endif // UI_LIST_H