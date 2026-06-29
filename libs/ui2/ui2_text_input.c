#include "ui2_text_input.h"
#include "ui2_graphical.h"
#include "hardware.h"
#include <stdlib.h>
#include <string.h>

#define KEY_ESC 27
#define KEY_BS 8
#define KEY_DEL 127

static void ui2_text_input_draw(ui2_widget_t *widget) {
    ui2_text_input_t *input = (ui2_text_input_t *)widget;
    if (!widget->visible || !input->buffer) return;

    if (ui2_is_graphical()) {
        int px = ui2_graphical_px(widget->x);
        int py = ui2_graphical_py(widget->y);
        int pw = ui2_graphical_pw(widget->width);
        int ph = ui2_graphical_ph(widget->height);
        int ch = text_mode_get_char_height();
        uint16_t label_fg = ui2_graphical_color(input->label_fg);
        uint16_t label_bg = ui2_graphical_color(input->label_bg);
        uint16_t title_fg = ui2_graphical_color(input->title_fg);
        uint16_t text_fg = ui2_graphical_color(input->text_fg);
        uint16_t hint_fg = ui2_graphical_color(input->hint_fg);

        display_fill_rect(px, py, pw, ph, label_bg);

        int current_py = py;

        if (input->title) {
            int tlen = strlen(input->title);
            int tx = px + (pw - tlen * text_mode_get_char_width()) / 2;
            if (tx < px) tx = px;
            display_draw_text(tx, current_py, input->title, title_fg);
            current_py += ch;
        }

        if (input->label) {
            display_draw_text(px, current_py, input->label, label_fg);
            int label_px = px + strlen(input->label) * text_mode_get_char_width() + 2;

            char shown[72];
            const char *source = input->buffer;
            size_t src_len = strlen(source);
            size_t copy_len = src_len;
            if (copy_len > sizeof(shown) - 3) copy_len = sizeof(shown) - 3;

            if (input->mask_input) {
                for (size_t i = 0; i < copy_len; i++) shown[i] = '*';
            } else {
                memcpy(shown, source, copy_len);
            }
            shown[copy_len] = '_';
            shown[copy_len + 1] = '\0';

            display_draw_text(label_px, current_py, shown, text_fg);
            current_py += ch;
        }

        const char *hint_left = input->hint_left ? input->hint_left : "Enter to confirm";
        const char *hint_right = input->hint_right ? input->hint_right : "ESC cancel";

        display_draw_text(px, current_py, hint_left, hint_fg);
        if (hint_right && hint_right[0]) {
            int rlen = strlen(hint_right);
            int rx = px + pw - rlen * text_mode_get_char_width() - 2;
            if (rx > px)
                display_draw_text(rx, current_py, hint_right, hint_fg);
        }
        return;
    }

    int x = widget->x, y = widget->y, width = widget->width, height = widget->height;

    for (int row = 0; row < height; row++)
        for (int col = 0; col < width; col++)
            text_mode_print_at_attr_bg(x + col, y + row, " ",
                                       input->label_fg, input->label_bg, TEXT_ATTR_NORMAL);

    int current_y = y;

    if (input->title) {
        int tlen = (int)strlen(input->title);
        int tx = x + (width - tlen) / 2;
        if (tx < x) tx = x;
        text_mode_print_at_attr_bg(tx, current_y, input->title,
                                   input->title_fg, input->title_bg, TEXT_ATTR_BOLD);
        current_y++;
    }

    if (input->label) {
        text_mode_print_at_attr_bg(x, current_y, input->label,
                                   input->label_fg, input->label_bg, TEXT_ATTR_BOLD);
        int label_x = x + (int)strlen(input->label) + 1;

        char shown[64];
        const char *source = input->buffer;
        size_t src_len = strlen(source);
        size_t copy_len = src_len;
        if (copy_len > sizeof(shown) - 2) copy_len = sizeof(shown) - 2;

        if (input->mask_input) {
            for (size_t i = 0; i < copy_len; i++) shown[i] = '*';
        } else {
            memcpy(shown, source, copy_len);
        }
        shown[copy_len] = '_';
        shown[copy_len + 1] = '\0';

        text_mode_print_at_attr_bg(label_x, current_y, shown,
                                   input->text_fg, input->text_bg, TEXT_ATTR_NORMAL);
        current_y++;
    }

    const char *hint_left = input->hint_left ? input->hint_left : "Enter to confirm";
    const char *hint_right = input->hint_right ? input->hint_right : "ESC cancel";

    text_mode_print_at_attr_bg(x, current_y, hint_left,
                               input->hint_fg, input->hint_bg, TEXT_ATTR_NORMAL);
    if (hint_right && hint_right[0]) {
        int rlen = (int)strlen(hint_right);
        int rx = x + width - rlen;
        if (rx > x)
            text_mode_print_at_attr_bg(rx, current_y, hint_right,
                                       input->hint_fg, input->hint_bg, TEXT_ATTR_NORMAL);
    }
}

static bool ui2_text_input_handle_key(ui2_widget_t *widget, char key) {
    ui2_text_input_t *input = (ui2_text_input_t *)widget;
    if (!widget->enabled || !input->buffer) return false;

    if (key == KEY_ESC) {
        if (input->on_cancel) input->on_cancel(input->cb_data);
        return true;
    }

    if (key == '\n' || key == '\r') {
        if (input->on_confirm) input->on_confirm(input->cb_data);
        return true;
    }

    if (key == KEY_BS || key == KEY_DEL || key == 0x08 || key == 0x7F) {
        size_t len = strlen(input->buffer);
        if (len > 0) input->buffer[len - 1] = '\0';
        return true;
    }

    if (key >= 32 && key <= 126) {
        size_t len = strlen(input->buffer);
        if (len < (size_t)(input->max_len - 1)) {
            input->buffer[len] = key;
            input->buffer[len + 1] = '\0';
        }
        return true;
    }

    return false;
}

static bool ui2_text_input_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed) {
    (void)col;
    (void)row;
    (void)pressed;
    ui2_text_input_t *input = (ui2_text_input_t *)widget;
    if (!widget->enabled || !widget->visible || !pressed) return false;
    return true;
}

static void ui2_text_input_on_focus(ui2_widget_t *widget, bool focused) {
    ui2_text_input_t *input = (ui2_text_input_t *)widget;
    if (focused) {
        input->text_fg = TEXT_COLOR_BRIGHT_WHITE;
        input->text_bg = TEXT_COLOR_BLUE;
    } else {
        input->text_fg = TEXT_COLOR_BRIGHT_GREEN;
        input->text_bg = TEXT_COLOR_BLACK;
    }
}

static const ui2_widget_vtable_t text_input_vtable = {
    .draw = ui2_text_input_draw,
    .handle_key = ui2_text_input_handle_key,
    .handle_touch = ui2_text_input_handle_touch,
    .on_focus = ui2_text_input_on_focus,
    .destroy = ui2_text_input_destroy
};

ui2_text_input_t *ui2_text_input_create(int x, int y, int width, int height) {
    ui2_text_input_t *input = (ui2_text_input_t *)calloc(1, sizeof(ui2_text_input_t));
    if (!input) return NULL;

    input->base.vtable = &text_input_vtable;
    input->base.x = x;
    input->base.y = y;
    input->base.width = width;
    input->base.height = height;
    input->base.visible = true;
    input->base.enabled = true;
    input->base.focusable = true;
    input->base.children = NULL;
    input->base.child_count = 0;
    input->base.user_data = NULL;

    input->buffer = NULL;
    input->max_len = 0;
    input->title = NULL;
    input->label = NULL;
    input->hint_left = NULL;
    input->hint_right = NULL;
    input->mask_input = false;

    input->title_fg = TEXT_COLOR_BRIGHT_CYAN;
    input->title_bg = TEXT_COLOR_BLACK;
    input->label_fg = TEXT_COLOR_WHITE;
    input->label_bg = TEXT_COLOR_BLACK;
    input->text_fg = TEXT_COLOR_BRIGHT_GREEN;
    input->text_bg = TEXT_COLOR_BLACK;
    input->hint_fg = TEXT_COLOR_WHITE;
    input->hint_bg = TEXT_COLOR_BLACK;

    input->on_confirm = NULL;
    input->on_cancel = NULL;
    input->cb_data = NULL;

    return input;
}

void ui2_text_input_destroy(ui2_widget_t *widget) {
    if (!widget) return;
    ui2_text_input_t *input = (ui2_text_input_t *)widget;
    free(input->title);
    free(input->label);
    free(input->hint_left);
    free(input->hint_right);
    free(input);
}

void ui2_text_input_set_buffer(ui2_text_input_t *input, char *buffer, int max_len) {
    if (!input) return;
    input->buffer = buffer;
    input->max_len = max_len;
}

void ui2_text_input_set_title(ui2_text_input_t *input, const char *title) {
    if (!input) return;
    free(input->title);
    input->title = NULL;
    if (title) {
        size_t len = strlen(title);
        input->title = (char *)malloc(len + 1);
        if (input->title) memcpy(input->title, title, len + 1);
    }
}

void ui2_text_input_set_label(ui2_text_input_t *input, const char *label) {
    if (!input) return;
    free(input->label);
    input->label = NULL;
    if (label) {
        size_t len = strlen(label);
        input->label = (char *)malloc(len + 1);
        if (input->label) memcpy(input->label, label, len + 1);
    }
}

void ui2_text_input_set_hints(ui2_text_input_t *input, const char *left, const char *right) {
    if (!input) return;
    free(input->hint_left);
    free(input->hint_right);
    input->hint_left = NULL;
    input->hint_right = NULL;
    if (left) {
        size_t len = strlen(left);
        input->hint_left = (char *)malloc(len + 1);
        if (input->hint_left) memcpy(input->hint_left, left, len + 1);
    }
    if (right) {
        size_t len = strlen(right);
        input->hint_right = (char *)malloc(len + 1);
        if (input->hint_right) memcpy(input->hint_right, right, len + 1);
    }
}

void ui2_text_input_set_mask(ui2_text_input_t *input, bool mask) {
    if (!input) return;
    input->mask_input = mask;
}

void ui2_text_input_set_colors(ui2_text_input_t *input,
                                uint8_t title_fg, uint8_t title_bg,
                                uint8_t label_fg, uint8_t label_bg,
                                uint8_t text_fg, uint8_t text_bg,
                                uint8_t hint_fg, uint8_t hint_bg) {
    if (!input) return;
    input->title_fg = title_fg;
    input->title_bg = title_bg;
    input->label_fg = label_fg;
    input->label_bg = label_bg;
    input->text_fg = text_fg;
    input->text_bg = text_bg;
    input->hint_fg = hint_fg;
    input->hint_bg = hint_bg;
}

void ui2_text_input_set_callbacks(ui2_text_input_t *input,
                                   ui2_text_input_cb on_confirm,
                                   ui2_text_input_cb on_cancel,
                                   void *user_data) {
    if (!input) return;
    input->on_confirm = on_confirm;
    input->on_cancel = on_cancel;
    input->cb_data = user_data;
}

void ui2_text_input_clear(ui2_text_input_t *input) {
    if (!input || !input->buffer) return;
    input->buffer[0] = '\0';
}
