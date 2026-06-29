#include "ui2_label.h"
#include "ui2_graphical.h"
#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ui2_label_draw(ui2_widget_t *widget) {
    ui2_label_t *label = (ui2_label_t *)widget;
    if (!widget->visible || !label->text) return;

    if (ui2_is_graphical()) {
        int px = ui2_graphical_px(widget->x);
        int py = ui2_graphical_py(widget->y);
        uint16_t fg = ui2_graphical_color(label->color);
        display_draw_text(px, py, label->text, fg);
        return;
    }

    for (int cx = 0; cx < widget->width; cx++) {
        text_mode_print_at_attr_bg(widget->x + cx, widget->y, " ", TEXT_COLOR_BLACK, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
    }

    text_mode_print_at_attr_bg(widget->x, widget->y, label->text, label->color, TEXT_COLOR_BLACK, label->attr);
}

static const ui2_widget_vtable_t label_vtable = {
    .draw = ui2_label_draw,
    .handle_key = ui2_widget_default_handle_key,
    .handle_touch = ui2_widget_default_handle_touch,
    .on_focus = ui2_widget_default_on_focus,
    .destroy = ui2_label_destroy
};

ui2_label_t *ui2_label_create(int x, int y, const char *text, uint8_t color, uint8_t attr) {
    ui2_label_t *label = (ui2_label_t *)malloc(sizeof(ui2_label_t));
    if (!label) return NULL;

    label->base.vtable = &label_vtable;
    label->base.x = x;
    label->base.y = y;
    label->base.width = text ? (int)strlen(text) : 0;
    label->base.height = 1;
    label->base.visible = true;
    label->base.enabled = true;
    label->base.focusable = false;
    label->base.children = NULL;
    label->base.child_count = 0;
    label->base.user_data = NULL;
    label->color = color;
    label->attr = attr;

    if (text) {
        size_t len = strlen(text);
        label->text = (char *)malloc(len + 1);
        if (label->text) memcpy(label->text, text, len + 1);
        else label->text = NULL;
    } else {
        label->text = NULL;
    }

    return label;
}

void ui2_label_destroy(ui2_widget_t *widget) {
    if (!widget) return;
    ui2_label_t *label = (ui2_label_t *)widget;
    free(label->text);
    free(label);
}

void ui2_label_set_text(ui2_label_t *label, const char *text) {
    if (!label) return;
    free(label->text);
    label->text = NULL;
    if (text) {
        size_t len = strlen(text);
        label->text = (char *)malloc(len + 1);
        if (label->text) memcpy(label->text, text, len + 1);
    }
    label->base.width = text ? (int)strlen(text) : 0;
}

void ui2_label_set_color(ui2_label_t *label, uint8_t color, uint8_t attr) {
    if (!label) return;
    label->color = color;
    label->attr = attr;
}
