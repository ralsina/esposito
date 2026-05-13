#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "text_mode.h"

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

// Clear screen to black
void ui_clear(void);

#ifdef __cplusplus
}
#endif

#endif
