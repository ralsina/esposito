#include "ui2_button.h"
#include "ui2_graphical.h"
#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ui2_button_draw(ui2_widget_t *widget) {
    ui2_button_t *btn = (ui2_button_t *)widget;
    if (!widget->visible) return;

    if (ui2_is_graphical()) {
        int px = ui2_graphical_px(widget->x);
        int py = ui2_graphical_py(widget->y);
        int pw = ui2_graphical_pw(widget->width);
        int ph = ui2_graphical_ph(widget->height);
        uint16_t bg = ui2_graphical_color(btn->bg_color);
        uint16_t fg = ui2_graphical_color(btn->fg_color);

        int r = 2;
        if (pw < 6 || ph < 6) r = 1;
        uint16_t border = ui2_lighten_color(bg);
        ui2_draw_rounded_rect(px, py, pw, ph, r, bg, border);

        if (btn->text) {
            int cx = px + pw / 2;
            int cy = py + ph / 2;
            display_draw_text_centered(cx, cy, btn->text, fg, bg);
            ui2_draw_rounded_rect_border(px, py, pw, ph, r, border);
        }
        return;
    }

    for (int dy = 0; dy < widget->height; dy++) {
        for (int dx = 0; dx < widget->width; dx++) {
            uint8_t attr = TEXT_ATTR_NORMAL;
            if (dy == 0) attr |= TEXT_ATTR_BORDER_TOP;
            if (dy == widget->height - 1) attr |= TEXT_ATTR_UNDERLINE;
            if (dx == 0) attr |= TEXT_ATTR_BORDER_LEFT;
            if (dx == widget->width - 1) attr |= TEXT_ATTR_BORDER_RIGHT;

            text_mode_print_at_attr_bg(widget->x + dx, widget->y + dy, " ",
                                       btn->fg_color, btn->bg_color, attr);
        }
    }

    if (btn->text) {
        int display_cols = 0;
        for (int i = 0; btn->text[i]; ) {
            unsigned char b = (unsigned char)btn->text[i];
            if ((b & 0xE0) == 0xC0) i += 2;
            else if ((b & 0xF0) == 0xE0) i += 3;
            else if ((b & 0xF8) == 0xF0) i += 4;
            else i += 1;
            display_cols++;
        }

        int text_x = widget->x + (widget->width - display_cols) / 2;
        int text_y = widget->y + (widget->height - 1) / 2;

        uint8_t text_attr = TEXT_ATTR_BOLD;
        if (text_y == widget->y) text_attr |= TEXT_ATTR_BORDER_TOP;
        if (text_y == widget->y + widget->height - 1) text_attr |= TEXT_ATTR_UNDERLINE;
        if (text_x == widget->x) text_attr |= TEXT_ATTR_BORDER_LEFT;
        if (text_x == widget->x + widget->width - 1) text_attr |= TEXT_ATTR_BORDER_RIGHT;

        text_mode_print_at_attr_bg(text_x, text_y, btn->text,
                                   btn->fg_color, btn->bg_color, text_attr);
    }
}

static bool ui2_button_handle_key(ui2_widget_t *widget, char key) {
    ui2_button_t *btn = (ui2_button_t *)widget;
    if (!widget->enabled || !widget->visible) return false;

    if (key == '\n' || key == '\r') {
        if (btn->on_click) btn->on_click(btn, btn->click_data);
        return true;
    }
    return false;
}

static bool ui2_button_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed) {
    ui2_button_t *btn = (ui2_button_t *)widget;
    if (!widget->enabled || !widget->visible || !pressed) return false;
    if (col < widget->x || col >= widget->x + widget->width) return false;
    if (row < widget->y || row >= widget->y + widget->height) return false;

    if (btn->on_click) btn->on_click(btn, btn->click_data);
    return true;
}

static void ui2_button_on_focus(ui2_widget_t *widget, bool focused) {
    ui2_button_t *btn = (ui2_button_t *)widget;
    if (focused) {
        btn->fg_color = TEXT_COLOR_BLACK;
        btn->bg_color = TEXT_COLOR_BRIGHT_GREEN;
    } else {
        btn->fg_color = TEXT_COLOR_BRIGHT_WHITE;
        btn->bg_color = TEXT_COLOR_BLUE;
    }
}

static const ui2_widget_vtable_t button_vtable = {
    .draw = ui2_button_draw,
    .handle_key = ui2_button_handle_key,
    .handle_touch = ui2_button_handle_touch,
    .on_focus = ui2_button_on_focus,
    .destroy = ui2_button_destroy
};

ui2_button_t *ui2_button_create(int x, int y, int width, int height, const char *text) {
    ui2_button_t *btn = (ui2_button_t *)malloc(sizeof(ui2_button_t));
    if (!btn) return NULL;

    btn->base.vtable = &button_vtable;
    btn->base.x = x;
    btn->base.y = y;
    btn->base.width = width;
    btn->base.height = height;
    btn->base.visible = true;
    btn->base.enabled = true;
    btn->base.focusable = true;
    btn->base.children = NULL;
    btn->base.child_count = 0;
    btn->base.user_data = NULL;
    btn->fg_color = TEXT_COLOR_BRIGHT_WHITE;
    btn->bg_color = TEXT_COLOR_BLUE;
    btn->on_click = NULL;
    btn->click_data = NULL;

    if (text) {
        size_t len = strlen(text);
        btn->text = (char *)malloc(len + 1);
        if (btn->text) memcpy(btn->text, text, len + 1);
        else btn->text = NULL;
    } else {
        btn->text = NULL;
    }

    return btn;
}

void ui2_button_destroy(ui2_widget_t *widget) {
    if (!widget) return;
    ui2_button_t *btn = (ui2_button_t *)widget;
    free(btn->text);
    free(btn);
}

void ui2_button_set_text(ui2_button_t *btn, const char *text) {
    if (!btn) return;
    free(btn->text);
    btn->text = NULL;
    if (text) {
        size_t len = strlen(text);
        btn->text = (char *)malloc(len + 1);
        if (btn->text) memcpy(btn->text, text, len + 1);
    }
}

void ui2_button_set_callback(ui2_button_t *btn, void (*cb)(ui2_button_t *, void *), void *data) {
    if (!btn) return;
    btn->on_click = cb;
    btn->click_data = data;
}

void ui2_button_set_colors(ui2_button_t *btn, uint16_t fg, uint16_t bg) {
    if (!btn) return;
    btn->fg_color = fg;
    btn->bg_color = bg;
}
