#include "ui2_widget.h"

bool ui2_widget_default_handle_key(ui2_widget_t *widget, char key) {
    (void)widget;
    (void)key;
    return false;
}

bool ui2_widget_default_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed) {
    (void)widget;
    (void)col;
    (void)row;
    (void)pressed;
    return false;
}

void ui2_widget_default_on_focus(ui2_widget_t *widget, bool focused) {
    (void)widget;
    (void)focused;
}
