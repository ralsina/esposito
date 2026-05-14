#include "ui.h"
#include "text_mode.h"
#include <string.h>
#include <stdio.h>

#define KEY_ESC 27
#define KEY_BS  8
#define KEY_DEL 127

void ui_window(int x, int y, int w, int h, const char *title) {
    int x2 = x + w - 1;
    int y2 = y + h - 1;

    for (int cx = x; cx <= x2; cx++) {
        text_mode_print_at_color(cx, y, "-", TEXT_COLOR_CYAN);
        text_mode_print_at_color(cx, y2, "-", TEXT_COLOR_CYAN);
    }
    for (int cy = y; cy <= y2; cy++) {
        text_mode_print_at_color(x, cy, "|", TEXT_COLOR_CYAN);
        text_mode_print_at_color(x2, cy, "|", TEXT_COLOR_CYAN);
    }
    text_mode_print_at_color(x, y, "+", TEXT_COLOR_CYAN);
    text_mode_print_at_color(x2, y, "+", TEXT_COLOR_CYAN);
    text_mode_print_at_color(x, y2, "+", TEXT_COLOR_CYAN);
    text_mode_print_at_color(x2, y2, "+", TEXT_COLOR_CYAN);

    if (title && title[0]) {
        int tlen = (int)strlen(title);
        int tx = x + (w - tlen) / 2;
        if (tx < x) tx = x + 1;
        text_mode_print_at_attr(tx, y, title, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    }
}

void ui_separator(int y) {
    int cols = text_mode_get_cols();
    for (int x = 0; x < cols; x++) {
        text_mode_print_at_color(x, y, "-", TEXT_COLOR_BLUE);
    }
}

void ui_label(int x, int y, const char *text, uint8_t color) {
    text_mode_print_at_color(x, y, text, color);
}

void ui_label_attr(int x, int y, const char *text, uint8_t color, uint8_t attr) {
    text_mode_print_at_attr(x, y, text, color, attr);
}

int ui_menu_draw(int x, int y, int max_rows, const char **items, int count, int selected) {
    int drawn = 0;
    for (int i = 0; i < count && drawn < max_rows; i++, drawn++) {
        uint16_t color = (i == selected) ? TEXT_COLOR_GREEN : TEXT_COLOR_WHITE;
        char marker = (i == selected) ? '>' : ' ';
        text_mode_printf_at_color(x, y + drawn, color, "%c %s", marker, items[i]);
    }
    return drawn;
}

void ui_status_bar(int y, const char *left, const char *right) {
    int cols = text_mode_get_cols();
    text_mode_print_at_color(2, y, left ? left : "", TEXT_COLOR_WHITE);
    if (right && right[0]) {
        int rlen = (int)strlen(right);
        int rx = cols - 2 - rlen;
        if (rx > 0) {
            text_mode_print_at_color(rx, y, right, TEXT_COLOR_WHITE);
        }
    }
}

int ui_text_input_handle(char key, char *buffer, int max_len) {
    if (key == KEY_ESC) {
        return -1;
    }
    if (key == '\n' || key == '\r') {
        return 1;
    }
    // Handle backspace: 0x08, 0x7F, or common terminal codes
    if (key == KEY_BS || key == KEY_DEL || key == 0x08 || key == 0x7F) {
        size_t len = strlen(buffer);
        if (len > 0) buffer[len - 1] = '\0';
        return 0;
    }
    if (key >= 32 && key <= 126) {
        size_t len = strlen(buffer);
        if (len < (size_t)(max_len - 1)) {
            buffer[len] = key;
            buffer[len + 1] = '\0';
        }
        return 0;
    }
    return 0;
}

void ui_text_input_widget_draw(const ui_text_input_widget_t *widget) {
    if (!widget || !widget->buffer || widget->max_len <= 0) {
        return;
    }

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    // Draw in bottom 4 rows instead of clearing entire screen
    int input_start_row = rows - 4;

    // Clear the input area
    for (int row = input_start_row; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            text_mode_print_at_attr_bg(col, row, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
        }
    }

    int y = input_start_row;

    // Title
    const char *title = widget->title ? widget->title : "Text Input";
    int tlen = (int)strlen(title);
    int tx = (cols - tlen) / 2;
    if (tx < 0) tx = 0;
    text_mode_print_at_attr_bg(tx, y++, title, TEXT_COLOR_BRIGHT_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);

    // Label and input value on next row
    const char *label = widget->label ? widget->label : "Value:";
    text_mode_print_at_attr_bg(2, y, label, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);

    // Build the display string (masked if needed)
    char shown[64];
    const char *source = widget->buffer;
    size_t src_len = strlen(source);
    size_t copy_len = src_len;
    if (copy_len > sizeof(shown) - 2) {
        copy_len = sizeof(shown) - 2;
    }

    if (widget->mask_input) {
        for (size_t index = 0; index < copy_len; index++) {
            shown[index] = '*';
        }
    } else {
        memcpy(shown, source, copy_len);
    }
    shown[copy_len] = '_';
    shown[copy_len + 1] = '\0';

    int value_x = 2 + (int)strlen(label) + 1;
    text_mode_print_at_attr_bg(value_x, y, shown, TEXT_COLOR_BRIGHT_GREEN, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
    y++;

    // Hints at bottom
    const char *hint_left = widget->hint_left ? widget->hint_left : "Type to enter  Enter Confirm";
    const char *hint_right = widget->hint_right ? widget->hint_right : "ESC Cancel";

    text_mode_print_at_attr_bg(2, y, hint_left, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
    if (hint_right && hint_right[0]) {
        int rlen = (int)strlen(hint_right);
        int rx = cols - 2 - rlen;
        if (rx > 2) {
            text_mode_print_at_attr_bg(rx, y, hint_right, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
        }
    }
}

int ui_text_input_widget_handle_event(const ui_text_input_widget_t *widget, const event_t *event) {
    if (!widget || !event || !widget->buffer || widget->max_len <= 0) {
        return 0;
    }

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) {
        return 0;
    }

    if (event->keyboard.modifiers & MODIFIER_CTRL) {
        return 0;
    }

    char key = event->keyboard.key;
    uint8_t raw_code = event->keyboard.raw_key_code;

    // DEBUG: Log what we received
    os_log("ui_widget", "key='%c'(0x%02x) raw_key_code=0x%02x", (key >= 32 && key <= 126) ? key : '?', (unsigned char)key, raw_code);

    // Check raw key code FIRST, before checking the mapped ASCII key
    // This avoids mistaking non-ASCII keys that are mapped to printable chars (like backspace -> 'E')
    // Also check key == KEY_BS directly since raw_key_code may not be populated in all paths
    if (raw_code == 0x08 || key == KEY_BS || key == KEY_DEL || key == 0x7F) {
        // Backspace
        size_t len = strlen(widget->buffer);
        if (len > 0) widget->buffer[len - 1] = '\0';
        ui_text_input_widget_draw(widget);
        text_mode_flush();
        return 0;
    }

    // Handle ESC
    if (key == KEY_ESC) {
        return -1;
    }

    // Handle Enter
    if (key == '\n' || key == '\r') {
        return 1;
    }

    // Handle regular character input (only if it's a printable character)
    if (key >= 32 && key <= 126) {
        size_t len = strlen(widget->buffer);
        if (len < (size_t)(widget->max_len - 1)) {
            widget->buffer[len] = key;
            widget->buffer[len + 1] = '\0';
        }
        ui_text_input_widget_draw(widget);
        text_mode_flush();
        return 0;
    }

    // Ignore other keys
    return 0;
}

void ui_clear(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
}
