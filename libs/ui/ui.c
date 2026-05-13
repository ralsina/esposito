#include "ui.h"
#include "text_mode.h"
#include <string.h>

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
    for (int x = 0; x < TEXT_MODE_COLS; x++) {
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
    text_mode_print_at_color(2, y, left ? left : "", TEXT_COLOR_WHITE);
    if (right && right[0]) {
        int rlen = (int)strlen(right);
        int rx = TEXT_MODE_COLS - 2 - rlen;
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
    if (key == KEY_BS || key == KEY_DEL) {
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

void ui_clear(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
}
