#include "ui2_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ui2_buffer_line_t *logical_line(ui2_buffer_t *buf, int idx) {
    return &buf->lines[(buf->head + idx) % buf->max_lines];
}

static void adjust_scroll(ui2_buffer_t *buf) {
    int max_offset = buf->count - buf->visible_rows;
    if (max_offset < 0) max_offset = 0;
    if (buf->scroll_to_bottom)
        buf->scroll_offset = max_offset;
    if (buf->scroll_offset < 0) buf->scroll_offset = 0;
    if (buf->scroll_offset > max_offset) buf->scroll_offset = max_offset;
}

static void ui2_buffer_draw(ui2_widget_t *widget) {
    ui2_buffer_t *buf = (ui2_buffer_t *)widget;
    if (!widget->visible) return;

    for (int i = 0; i < buf->visible_rows; i++) {
        int logical_idx = i + buf->scroll_offset;
        int y = widget->y + i;

        if (logical_idx < buf->count) {
            ui2_buffer_line_t *line = logical_line(buf, logical_idx);
            // Fill row background
            for (int cx = 0; cx < widget->width; cx++) {
                text_mode_print_at_attr_bg(widget->x + cx, y, " ",
                    line->fg, line->bg, TEXT_ATTR_NORMAL);
            }
            // Print line text
            text_mode_print_at_attr_bg(widget->x, y, line->text,
                line->fg, line->bg, line->attrs);
        } else {
            // Clear remaining rows
            for (int cx = 0; cx < widget->width; cx++) {
                text_mode_print_at_attr_bg(widget->x + cx, y, " ",
                    TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
            }
        }
    }
}

static bool ui2_buffer_handle_key(ui2_widget_t *widget, char key) {
    ui2_buffer_t *buf = (ui2_buffer_t *)widget;
    if (!widget->enabled) return false;

    if (key == 'w' || key == 'W' || (unsigned char)key == 0x99) {
        int max_offset = buf->count - buf->visible_rows;
        if (max_offset < 0) max_offset = 0;
        if (buf->scroll_offset < max_offset) {
            buf->scroll_offset++;
            buf->scroll_to_bottom = false;
        }
        return true;
    }

    if (key == 's' || key == 'S' || (unsigned char)key == 0x98) {
        if (buf->scroll_offset > 0) {
            buf->scroll_offset--;
            adjust_scroll(buf);
        }
        return true;
    }

    return false;
}

static bool ui2_buffer_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed) {
    ui2_buffer_t *buf = (ui2_buffer_t *)widget;
    if (!widget->enabled || !widget->visible || !pressed) return false;

    // Touch anywhere scrolls: top half scrolls up, bottom half scrolls down
    int local_y = row - widget->y;
    if (local_y < 0 || local_y >= buf->visible_rows) return false;
    if (col < widget->x || col >= widget->x + widget->width) return false;

    int midpoint = widget->y + buf->visible_rows / 2;
    if (row < midpoint && buf->scroll_offset < buf->count - buf->visible_rows) {
        buf->scroll_offset++;
        buf->scroll_to_bottom = false;
    } else if (row >= midpoint && buf->scroll_offset > 0) {
        buf->scroll_offset--;
        adjust_scroll(buf);
    }
    return true;
}

static const ui2_widget_vtable_t buffer_vtable = {
    .draw = ui2_buffer_draw,
    .handle_key = ui2_buffer_handle_key,
    .handle_touch = ui2_buffer_handle_touch,
    .on_focus = ui2_widget_default_on_focus,
    .destroy = ui2_buffer_destroy
};

ui2_buffer_t *ui2_buffer_create(int x, int y, int width, int height, int max_lines) {
    ui2_buffer_t *buf = (ui2_buffer_t *)calloc(1, sizeof(ui2_buffer_t));
    if (!buf) return NULL;

    buf->base.vtable = &buffer_vtable;
    buf->base.x = x;
    buf->base.y = y;
    buf->base.width = width;
    buf->base.height = height;
    buf->base.visible = true;
    buf->base.enabled = true;
    buf->base.focusable = false;
    buf->base.children = NULL;
    buf->base.child_count = 0;
    buf->base.user_data = NULL;

    buf->max_lines = max_lines > 0 ? max_lines : 100;
    buf->lines = (ui2_buffer_line_t *)calloc((size_t)buf->max_lines, sizeof(ui2_buffer_line_t));
    if (!buf->lines) {
        free(buf);
        return NULL;
    }

    buf->count = 0;
    buf->head = 0;
    buf->scroll_offset = 0;
    buf->visible_rows = height;
    buf->scroll_to_bottom = true;

    return buf;
}

void ui2_buffer_destroy(ui2_widget_t *widget) {
    if (!widget) return;
    ui2_buffer_t *buf = (ui2_buffer_t *)widget;
    for (int i = 0; i < buf->count; i++) {
        free(logical_line(buf, i)->text);
    }
    free(buf->lines);
    free(buf);
}

int ui2_buffer_add_line(ui2_buffer_t *buf, const char *text, uint8_t fg, uint8_t bg, uint8_t attrs) {
    if (!buf || !text) return -1;

    if (buf->count < buf->max_lines) {
        int idx = (buf->head + buf->count) % buf->max_lines;
        buf->lines[idx].text = strdup(text);
        buf->lines[idx].fg = fg;
        buf->lines[idx].bg = bg;
        buf->lines[idx].attrs = attrs;
        buf->count++;
    } else {
        free(buf->lines[buf->head].text);
        buf->lines[buf->head].text = strdup(text);
        buf->lines[buf->head].fg = fg;
        buf->lines[buf->head].bg = bg;
        buf->lines[buf->head].attrs = attrs;
        buf->head = (buf->head + 1) % buf->max_lines;
    }

    adjust_scroll(buf);
    return buf->count - 1;
}

void ui2_buffer_clear(ui2_buffer_t *buf) {
    if (!buf) return;
    for (int i = 0; i < buf->count; i++) {
        free(logical_line(buf, i)->text);
        logical_line(buf, i)->text = NULL;
    }
    buf->count = 0;
    buf->head = 0;
    buf->scroll_offset = 0;
    buf->scroll_to_bottom = true;
}

void ui2_buffer_scroll(ui2_buffer_t *buf, int delta) {
    if (!buf) return;
    buf->scroll_to_bottom = false;
    buf->scroll_offset += delta;
    int max_offset = buf->count - buf->visible_rows;
    if (max_offset < 0) max_offset = 0;
    if (buf->scroll_offset < 0) buf->scroll_offset = 0;
    if (buf->scroll_offset > max_offset) buf->scroll_offset = max_offset;
}

void ui2_buffer_set_scroll_to_bottom(ui2_buffer_t *buf, bool auto_scroll) {
    if (!buf) return;
    buf->scroll_to_bottom = auto_scroll;
    if (auto_scroll) adjust_scroll(buf);
}

int ui2_buffer_get_count(const ui2_buffer_t *buf) {
    return buf ? buf->count : 0;
}
