#include "ui_progressbar.h"
#include "os_core.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

ui_progressbar_t* ui_progressbar_create(int x, int y, int width) {
    ui_progressbar_t *bar = (ui_progressbar_t*)malloc(sizeof(ui_progressbar_t));
    if (!bar) {
        return NULL;
    }

    bar->x = x;
    bar->y = y;
    bar->width = width;
    bar->value = 0;
    bar->max_value = 100;
    bar->fg_color = TEXT_COLOR_GREEN;
    bar->bg_color = TEXT_COLOR_BRIGHT_BLACK;
    bar->border_color = TEXT_COLOR_CYAN;
    bar->show_percent = true;
    bar->visible = true;

    return bar;
}

void ui_progressbar_destroy(ui_progressbar_t *bar) {
    if (!bar) {
        return;
    }

    free(bar);
}

void ui_progressbar_set_value(ui_progressbar_t *bar, int value, int max_value) {
    if (!bar || max_value <= 0) {
        return;
    }

    bar->value = value;
    bar->max_value = max_value;

    if (bar->value < 0) bar->value = 0;
    if (bar->value > bar->max_value) bar->value = bar->max_value;
}

void ui_progressbar_set_colors(ui_progressbar_t *bar, uint8_t fg, uint8_t bg, uint8_t border) {
    if (!bar) {
        return;
    }

    bar->fg_color = fg;
    bar->bg_color = bg;
    bar->border_color = border;
}

void ui_progressbar_set_visible(ui_progressbar_t *bar, bool visible) {
    if (!bar) {
        return;
    }

    bar->visible = visible;
}

void ui_progressbar_draw(const ui_progressbar_t *bar) {
    if (!bar || !bar->visible) {
        return;
    }

    int filled = 0;
    if (bar->max_value > 0) {
        filled = (bar->value * bar->width) / bar->max_value;
    }

    if (filled < 0) filled = 0;
    if (filled > bar->width) filled = bar->width;

    for (int cx = 0; cx < bar->width; cx++) {
        uint8_t color;
        uint8_t attr = TEXT_ATTR_NORMAL;

        if (cx == 0) {
            attr |= TEXT_ATTR_BORDER_LEFT;
        }
        if (cx == bar->width - 1) {
            attr |= TEXT_ATTR_BORDER_RIGHT;
        }
        attr |= TEXT_ATTR_UNDERLINE;
        attr |= TEXT_ATTR_BORDER_TOP;

        if (cx < filled) {
            color = bar->fg_color;
        } else {
            color = bar->bg_color;
        }

        text_mode_print_at_attr_bg(bar->x + cx, bar->y, " ", color, bar->border_color, attr);
    }

    if (bar->show_percent && bar->max_value > 0) {
        int pct = (bar->value * 100) / bar->max_value;
        char pct_str[8];
        snprintf(pct_str, sizeof(pct_str), "%d%%", pct);

        int pct_x = bar->x + (bar->width - (int)strlen(pct_str)) / 2;
        if (pct_x < bar->x) pct_x = bar->x + 1;

        text_mode_print_at_attr(pct_x, bar->y, pct_str, bar->border_color, TEXT_ATTR_NORMAL);
    }
}
