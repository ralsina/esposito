#include "ui2_text.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static int border_offset(const ui2_text_t *text) {
    return text->draw_border ? 1 : 0;
}

static void ui2_text_draw(ui2_widget_t *widget) {
    ui2_text_t *text = (ui2_text_t *)widget;
    if (!widget->visible) return;

    for (int dy = 0; dy < widget->height; dy++) {
        for (int dx = 0; dx < widget->width; dx++) {
            uint8_t attr = TEXT_ATTR_NORMAL;
            if (text->draw_border) {
                if (dy == 0) attr |= TEXT_ATTR_BORDER_TOP;
                if (dy == widget->height - 1) attr |= TEXT_ATTR_UNDERLINE;
                if (dx == 0) attr |= TEXT_ATTR_BORDER_LEFT;
                if (dx == widget->width - 1) attr |= TEXT_ATTR_BORDER_RIGHT;
            }
            text_mode_print_at_attr_bg(widget->x + dx, widget->y + dy, " ",
                                       text->border_fg, TEXT_COLOR_BLACK, attr);
        }
    }
}

static const ui2_widget_vtable_t text_vtable = {
    .draw = ui2_text_draw,
    .handle_key = ui2_widget_default_handle_key,
    .handle_touch = ui2_widget_default_handle_touch,
    .on_focus = ui2_widget_default_on_focus,
    .destroy = ui2_text_destroy
};

ui2_text_t *ui2_text_create(int x, int y, int width, int height) {
    ui2_text_t *text = (ui2_text_t *)calloc(1, sizeof(ui2_text_t));
    if (!text) return NULL;

    text->base.vtable = &text_vtable;
    text->base.x = x;
    text->base.y = y;
    text->base.width = width;
    text->base.height = height;
    text->base.visible = true;
    text->base.enabled = true;
    text->base.focusable = false;
    text->base.children = NULL;
    text->base.child_count = 0;
    text->base.user_data = NULL;

    text->draw_border = false;
    text->border_fg = TEXT_COLOR_CYAN;

    return text;
}

void ui2_text_destroy(ui2_widget_t *widget) {
    free(widget);
}

void ui2_text_set_border(ui2_text_t *text, bool draw, uint8_t border_fg) {
    if (!text) return;
    text->draw_border = draw;
    text->border_fg = border_fg;
}

static int content_x(const ui2_text_t *text) {
    return text->base.x + border_offset(text);
}

static int content_y(const ui2_text_t *text) {
    return text->base.y + border_offset(text);
}

int ui2_text_get_content_width(const ui2_text_t *text) {
    if (!text) return 0;
    return text->base.width - border_offset(text) * 2;
}

int ui2_text_get_content_height(const ui2_text_t *text) {
    if (!text) return 0;
    return text->base.height - border_offset(text) * 2;
}

void ui2_text_print_at(ui2_text_t *text, int rel_x, int rel_y,
                        const char *str, uint8_t fg, uint8_t bg, uint8_t attr) {
    if (!text || !text->base.visible || !str) return;

    int cw = ui2_text_get_content_width(text);
    int ch = ui2_text_get_content_height(text);

    if (rel_x < 0 || rel_y < 0 || rel_y >= ch) return;
    if (rel_x >= cw) return;

    int max_chars = cw - rel_x;
    if (max_chars <= 0) return;

    int slen = (int)strlen(str);
    int n = slen < max_chars ? slen : max_chars;

    char buf[256];
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    memcpy(buf, str, n);
    buf[n] = '\0';

    text_mode_print_at_attr_bg(content_x(text) + rel_x, content_y(text) + rel_y,
                               buf, fg, bg, attr);
}

void ui2_text_printf_at(ui2_text_t *text, int rel_x, int rel_y,
                         uint8_t fg, uint8_t bg, uint8_t attr,
                         const char *fmt, ...) {
    if (!text || !text->base.visible || !fmt) return;

    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ui2_text_print_at(text, rel_x, rel_y, buf, fg, bg, attr);
}

void ui2_text_clear(ui2_text_t *text) {
    if (!text || !text->base.visible) return;

    int cw = ui2_text_get_content_width(text);
    int ch = ui2_text_get_content_height(text);
    int ox = content_x(text);
    int oy = content_y(text);

    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++)
            text_mode_print_at_attr_bg(ox + x, oy + y, " ",
                                       TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
}
