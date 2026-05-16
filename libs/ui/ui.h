#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "os_core.h"
#include "text_mode.h"
#include "ui_button.h"

#ifdef __cplusplus
extern "C" {
#endif

// Draw a bordered window with optional title
void ui_window(int x, int y, int w, int h, const char *title);

// Draw a horizontal separator line
void ui_separator(int y);

// Draw text at position
void ui_label(int x, int y, const char *text, uint8_t color);
void ui_label_attr(int x, int y, const char *text, uint8_t color, uint8_t attr);

// Draw a menu list with selection marker (> on selected item)
// Returns number of items drawn (for scroll tracking)
int ui_menu_draw(int x, int y, int max_rows, const char **items, int count, int selected);

// Draw a status bar at bottom row with left/right aligned text
void ui_status_bar(int y, const char *left, const char *right);

// Handle keyboard input for text entry
// Returns: 1 on Enter (confirm), 0 while editing, -1 on ESC (cancel)
int ui_text_input_handle(char key, char *buffer, int max_len);

// Stateful text input widget handled by the UI layer
typedef struct {
	const char *title;
	const char *label;
	char *buffer;
	int max_len;
	bool mask_input;
	const char *hint_left;
	const char *hint_right;
} ui_text_input_widget_t;

// Draw text input widget (does not flush)
void ui_text_input_widget_draw(const ui_text_input_widget_t *widget);

// Handle keyboard event for text input widget.
// Returns: 1 on Enter (confirm), 0 while editing/ignored, -1 on ESC (cancel)
// Redraws + flushes automatically while editing.
int ui_text_input_widget_handle_event(const ui_text_input_widget_t *widget, const event_t *event);

// Column widget for displaying scrollable lists
// Stateless: app owns the data (char** list), selection index, and scroll offset.
// Widget just renders from a given scroll offset with a selected row highlighted.

// Draw a column widget
// x, y: top-left position
// width, height: dimensions (including border)
// title: window title
// active: whether this column is active (affects highlight colors)
// items: pointer to array of strings (char**)
// count: number of items in the array
// selected: which item is currently selected (0-based)
// scroll_offset: which item is at the top of the display
void ui_column_draw(int x, int y, int width, int height, const char *title, int active,
                    const char **items, int count, int selected, int scroll_offset);

// Clear screen to black
void ui_clear(void);

#ifdef __cplusplus
}
#endif

#endif
