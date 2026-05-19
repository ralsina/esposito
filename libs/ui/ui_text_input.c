#include "ui_text_input.h"
#include "os_core.h"
#include <stdlib.h>
#include <string.h>

#define KEY_ESC 27
#define KEY_BS  8
#define KEY_DEL 127

ui_text_input_widget_t* ui_text_input_create(int x, int y, int width, int height) {
    ui_text_input_widget_t *widget = (ui_text_input_widget_t*)malloc(sizeof(struct ui_text_input_widget_t));
    if (!widget) {
        return NULL;
    }

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    widget->buffer = NULL;
    widget->max_len = 0;

    widget->title = NULL;
    widget->label = NULL;
    widget->mask_input = false;
    widget->hint_left = NULL;
    widget->hint_right = NULL;

    // Default colors
    widget->title_fg = TEXT_COLOR_BRIGHT_CYAN;
    widget->title_bg = TEXT_COLOR_BLACK;
    widget->label_fg = TEXT_COLOR_WHITE;
    widget->label_bg = TEXT_COLOR_BLACK;
    widget->text_fg = TEXT_COLOR_BRIGHT_GREEN;
    widget->text_bg = TEXT_COLOR_BLACK;
    widget->hint_fg = TEXT_COLOR_WHITE;
    widget->hint_bg = TEXT_COLOR_BLACK;

    widget->visible = true;
    widget->enabled = true;
    widget->focused = false;

    widget->on_text_changed = NULL;
    widget->on_confirm = NULL;
    widget->on_cancel = NULL;
    widget->user_data = NULL;

    return widget;
}

void ui_text_input_destroy(ui_text_input_widget_t *widget) {
    if (!widget) {
        return;
    }
    if (widget->title) {
        free(widget->title);
    }
    if (widget->label) {
        free(widget->label);
    }
    if (widget->hint_left) {
        free(widget->hint_left);
    }
    if (widget->hint_right) {
        free(widget->hint_right);
    }
    free(widget);
}

void ui_text_input_set_buffer(ui_text_input_widget_t *widget, char *buffer, int max_len) {
    if (!widget) {
        return;
    }
    widget->buffer = buffer;
    widget->max_len = max_len;
}

void ui_text_input_set_title(ui_text_input_widget_t *widget, const char *title) {
    if (!widget) {
        return;
    }

    if (widget->title) {
        free(widget->title);
        widget->title = NULL;
    }

    if (title) {
        size_t len = strlen(title);
        widget->title = (char*)malloc(len + 1);
        if (widget->title) {
            memcpy(widget->title, title, len + 1);
        }
    }
}

void ui_text_input_set_label(ui_text_input_widget_t *widget, const char *label) {
    if (!widget) {
        return;
    }

    if (widget->label) {
        free(widget->label);
        widget->label = NULL;
    }

    if (label) {
        size_t len = strlen(label);
        widget->label = (char*)malloc(len + 1);
        if (widget->label) {
            memcpy(widget->label, label, len + 1);
        }
    }
}

void ui_text_input_set_hints(ui_text_input_widget_t *widget, const char *hint_left, const char *hint_right) {
    if (!widget) {
        return;
    }

    if (widget->hint_left) {
        free(widget->hint_left);
        widget->hint_left = NULL;
    }
    if (widget->hint_right) {
        free(widget->hint_right);
        widget->hint_right = NULL;
    }

    if (hint_left) {
        size_t len = strlen(hint_left);
        widget->hint_left = (char*)malloc(len + 1);
        if (widget->hint_left) {
            memcpy(widget->hint_left, hint_left, len + 1);
        }
    }

    if (hint_right) {
        size_t len = strlen(hint_right);
        widget->hint_right = (char*)malloc(len + 1);
        if (widget->hint_right) {
            memcpy(widget->hint_right, hint_right, len + 1);
        }
    }
}

void ui_text_input_set_mask(ui_text_input_widget_t *widget, bool mask_input) {
    if (!widget) {
        return;
    }
    widget->mask_input = mask_input;
}

void ui_text_input_set_colors(ui_text_input_widget_t *widget,
                              uint8_t title_fg, uint8_t title_bg,
                              uint8_t label_fg, uint8_t label_bg,
                              uint8_t text_fg, uint8_t text_bg,
                              uint8_t hint_fg, uint8_t hint_bg) {
    if (!widget) {
        return;
    }

    widget->title_fg = title_fg;
    widget->title_bg = title_bg;
    widget->label_fg = label_fg;
    widget->label_bg = label_bg;
    widget->text_fg = text_fg;
    widget->text_bg = text_bg;
    widget->hint_fg = hint_fg;
    widget->hint_bg = hint_bg;
}

void ui_text_input_set_callbacks(ui_text_input_widget_t *widget,
                                 ui_text_input_cb on_text_changed,
                                 ui_text_input_cb on_confirm,
                                 ui_text_input_cb on_cancel,
                                 void *user_data) {
    if (!widget) {
        return;
    }

    widget->on_text_changed = on_text_changed;
    widget->on_confirm = on_confirm;
    widget->on_cancel = on_cancel;
    widget->user_data = user_data;
}

void ui_text_input_set_focus(ui_text_input_widget_t *widget, bool focused) {
    if (!widget) {
        return;
    }
    widget->focused = focused;
}

void ui_text_input_clear(ui_text_input_widget_t *widget) {
    if (!widget || !widget->buffer) {
        return;
    }
    widget->buffer[0] = '\0';
}

void ui_text_input_draw(const ui_text_input_widget_t *widget) {
    if (!widget || !widget->visible || !widget->buffer) {
        return;
    }

    int x = widget->x;
    int y = widget->y;
    int width = widget->width;
    int height = widget->height;

    // Clear the widget area using label background
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            text_mode_print_at_attr_bg(x + col, y + row, " ", widget->label_fg, widget->label_bg, TEXT_ATTR_NORMAL);
        }
    }

    int current_y = y;

    // Draw title
    if (widget->title) {
        int tlen = (int)strlen(widget->title);
        int tx = x + (width - tlen) / 2;
        if (tx < x) tx = x;
        text_mode_print_at_attr_bg(tx, current_y, widget->title, widget->title_fg, widget->title_bg, TEXT_ATTR_BOLD);
        current_y++;
    }

    // Draw label and input
    if (widget->label) {
        text_mode_print_at_attr_bg(x, current_y, widget->label, widget->label_fg, widget->label_bg, TEXT_ATTR_BOLD);
        int label_x = x + (int)strlen(widget->label) + 1;

        // Build the display string (masked if needed)
        char shown[64];
        const char *source = widget->buffer;
        size_t src_len = strlen(source);
        size_t copy_len = src_len;
        if (copy_len > sizeof(shown) - 2) {
            copy_len = sizeof(shown) - 2;
        }

        if (widget->mask_input) {
            for (size_t i = 0; i < copy_len; i++) {
                shown[i] = '*';
            }
        } else {
            memcpy(shown, source, copy_len);
        }
        shown[copy_len] = '_';
        shown[copy_len + 1] = '\0';

        text_mode_print_at_attr_bg(label_x, current_y, shown, widget->text_fg, widget->text_bg, TEXT_ATTR_NORMAL);
        current_y++;
    }

    // Draw hints at bottom
    const char *hint_left = widget->hint_left ? widget->hint_left : "Type to enter  Enter Confirm";
    const char *hint_right = widget->hint_right ? widget->hint_right : "ESC Cancel";

    text_mode_print_at_attr_bg(x, current_y, hint_left, widget->hint_fg, widget->hint_bg, TEXT_ATTR_NORMAL);
    if (hint_right && hint_right[0]) {
        int rlen = (int)strlen(hint_right);
        int rx = x + width - rlen;
        if (rx > x) {
            text_mode_print_at_attr_bg(rx, current_y, hint_right, widget->hint_fg, widget->hint_bg, TEXT_ATTR_NORMAL);
        }
    }
}

bool ui_text_input_handle_key(ui_text_input_widget_t *widget, char key) {
    if (!widget || !widget->enabled || !widget->buffer) {
        return false;
    }

    bool handled = false;

    if (key == KEY_ESC) {
        // Cancel
        if (widget->on_cancel) {
            widget->on_cancel(widget, widget->user_data);
        }
        handled = true;
    } else if (key == '\n' || key == '\r') {
        // Confirm
        if (widget->on_confirm) {
            widget->on_confirm(widget, widget->user_data);
        }
        handled = true;
    } else if (key == KEY_BS || key == KEY_DEL || key == 0x08 || key == 0x7F) {
        // Backspace
        size_t len = strlen(widget->buffer);
        if (len > 0) {
            widget->buffer[len - 1] = '\0';
            if (widget->on_text_changed) {
                widget->on_text_changed(widget, widget->user_data);
            }
        }
        handled = true;
    } else if (key >= 32 && key <= 126) {
        // Regular character input
        size_t len = strlen(widget->buffer);
        if (len < (size_t)(widget->max_len - 1)) {
            widget->buffer[len] = key;
            widget->buffer[len + 1] = '\0';
            if (widget->on_text_changed) {
                widget->on_text_changed(widget, widget->user_data);
            }
        }
        handled = true;
    }

    return handled;
}