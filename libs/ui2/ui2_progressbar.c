#include "ui2_progressbar.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void ui2_progressbar_draw(ui2_widget_t *widget) {
    ui2_progressbar_t *bar = (ui2_progressbar_t *)widget;
    if (!widget->visible) return;

    int filled = 0;
    if (bar->max_value > 0)
        filled = (bar->value * widget->width) / bar->max_value;

    if (filled < 0) filled = 0;
    if (filled > widget->width) filled = widget->width;

    for (int cx = 0; cx < widget->width; cx++) {
        uint8_t color;
        uint8_t attr = TEXT_ATTR_NORMAL;

        if (cx == 0) attr |= TEXT_ATTR_BORDER_LEFT;
        if (cx == widget->width - 1) attr |= TEXT_ATTR_BORDER_RIGHT;
        attr |= TEXT_ATTR_UNDERLINE;
        attr |= TEXT_ATTR_BORDER_TOP;

        color = (cx < filled) ? bar->fg_color : bar->bg_color;

        text_mode_print_at_attr_bg(widget->x + cx, widget->y, " ",
                                   color, bar->border_color, attr);
    }

    if (bar->show_percent && bar->max_value > 0) {
        int pct = (bar->value * 100) / bar->max_value;
        char pct_str[8];
        snprintf(pct_str, sizeof(pct_str), "%d%%", pct);

        int pct_x = widget->x + (widget->width - (int)strlen(pct_str)) / 2;
        if (pct_x < widget->x) pct_x = widget->x + 1;

        text_mode_print_at_attr(pct_x, widget->y, pct_str,
                                bar->border_color, TEXT_ATTR_NORMAL);
    }
}

static const ui2_widget_vtable_t progressbar_vtable = {
    .draw = ui2_progressbar_draw,
    .handle_key = ui2_widget_default_handle_key,
    .handle_touch = ui2_widget_default_handle_touch,
    .on_focus = ui2_widget_default_on_focus,
    .destroy = ui2_progressbar_destroy
};

ui2_progressbar_t *ui2_progressbar_create(int x, int y, int width) {
    ui2_progressbar_t *bar = (ui2_progressbar_t *)calloc(1, sizeof(ui2_progressbar_t));
    if (!bar) return NULL;

    bar->base.vtable = &progressbar_vtable;
    bar->base.x = x;
    bar->base.y = y;
    bar->base.width = width;
    bar->base.height = 1;
    bar->base.visible = true;
    bar->base.enabled = true;
    bar->base.focusable = false;
    bar->base.children = NULL;
    bar->base.child_count = 0;
    bar->base.user_data = NULL;

    bar->value = 0;
    bar->max_value = 100;
    bar->fg_color = TEXT_COLOR_GREEN;
    bar->bg_color = TEXT_COLOR_BRIGHT_BLACK;
    bar->border_color = TEXT_COLOR_CYAN;
    bar->show_percent = true;

    return bar;
}

void ui2_progressbar_destroy(ui2_widget_t *widget) {
    free(widget);
}

void ui2_progressbar_set_value(ui2_progressbar_t *bar, int value, int max_value) {
    if (!bar || max_value <= 0) return;
    bar->value = value;
    bar->max_value = max_value;
    if (bar->value < 0) bar->value = 0;
    if (bar->value > bar->max_value) bar->value = bar->max_value;
}

void ui2_progressbar_set_colors(ui2_progressbar_t *bar, uint8_t fg, uint8_t bg, uint8_t border) {
    if (!bar) return;
    bar->fg_color = fg;
    bar->bg_color = bg;
    bar->border_color = border;
}
