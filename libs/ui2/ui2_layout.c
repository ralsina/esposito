#include "ui2_layout.h"
#include <stdlib.h>
#include <string.h>

static void ui2_layout_draw(ui2_widget_t *widget) {
    ui2_layout_t *layout = (ui2_layout_t *)widget;
    if (!widget->visible) return;

    int cx = 0, cy = widget->y;

    if (layout->type == UI2_LAYOUT_HORIZONTAL) {
        int total_width = 0;
        for (int i = 0; i < widget->child_count; i++)
            total_width += widget->children[i]->width;
        int gaps = widget->child_count > 1 ? (widget->child_count - 1) * layout->gap : 0;
        int leftover = widget->width - total_width - gaps;
        cx = widget->x + (leftover > 0 ? leftover / 2 : 0);
    }

    for (int i = 0; i < widget->child_count; i++) {
        ui2_widget_t *child = widget->children[i];

        if (layout->type == UI2_LAYOUT_VERTICAL) {
            child->x = widget->x;
            child->width = widget->width;
            if (i == 0)
                cy = widget->y;
            else
                cy = widget->children[i - 1]->y + widget->children[i - 1]->height + layout->gap;
            child->y = cy;
        } else if (layout->type == UI2_LAYOUT_HORIZONTAL) {
            child->y = widget->y;
            child->height = widget->height;
            if (i > 0)
                cx += layout->gap;
            child->x = cx;
            cx += child->width;
        }

        child->vtable->draw(child);
    }
}

static bool ui2_layout_handle_key(ui2_widget_t *widget, char key) {
    (void)key;
    if (!widget->enabled) return false;
    return false;
}

static bool ui2_layout_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed) {
    if (!widget->enabled || !widget->visible) return false;
    return false;
}

static void ui2_layout_on_focus(ui2_widget_t *widget, bool focused) {
    (void)widget;
    (void)focused;
}

static const ui2_widget_vtable_t layout_vtable = {
    .draw = ui2_layout_draw,
    .handle_key = ui2_layout_handle_key,
    .handle_touch = ui2_layout_handle_touch,
    .on_focus = ui2_layout_on_focus,
    .destroy = ui2_layout_destroy
};

ui2_layout_t *ui2_layout_create(int x, int y, int width, int height, ui2_layout_type_t type) {
    ui2_layout_t *layout = (ui2_layout_t *)malloc(sizeof(ui2_layout_t));
    if (!layout) return NULL;

    layout->base.vtable = &layout_vtable;
    layout->base.x = x;
    layout->base.y = y;
    layout->base.width = width;
    layout->base.height = height;
    layout->base.visible = true;
    layout->base.enabled = true;
    layout->base.focusable = false;
    layout->base.children = NULL;
    layout->base.child_count = 0;
    layout->base.user_data = NULL;
    layout->type = type;
    layout->gap = 1;
    layout->capacity = 0;

    return layout;
}

void ui2_layout_destroy(ui2_widget_t *widget) {
    if (!widget) return;
    ui2_layout_t *layout = (ui2_layout_t *)widget;

    for (int i = 0; i < widget->child_count; i++) {
        if (widget->children[i] && widget->children[i]->vtable)
            widget->children[i]->vtable->destroy(widget->children[i]);
    }

    free(widget->children);
    free(layout);
}

void ui2_layout_add(ui2_layout_t *layout, ui2_widget_t *child) {
    if (!layout || !child) return;

    if (layout->base.child_count >= layout->capacity) {
        int new_cap = layout->capacity ? layout->capacity * 2 : 8;
        ui2_widget_t **new_children = (ui2_widget_t **)realloc(
            layout->base.children, new_cap * sizeof(ui2_widget_t *));
        if (!new_children) return;
        layout->base.children = new_children;
        layout->capacity = new_cap;
    }

    layout->base.children[layout->base.child_count++] = child;
}

void ui2_layout_remove(ui2_layout_t *layout, ui2_widget_t *child) {
    if (!layout || !child) return;

    int found = -1;
    for (int i = 0; i < layout->base.child_count; i++) {
        if (layout->base.children[i] == child) {
            found = i;
            break;
        }
    }

    if (found < 0) return;

    for (int i = found; i < layout->base.child_count - 1; i++)
        layout->base.children[i] = layout->base.children[i + 1];
    layout->base.child_count--;
}

void ui2_layout_set_gap(ui2_layout_t *layout, int gap) {
    if (!layout) return;
    layout->gap = gap;
}

void ui2_layout_clear(ui2_layout_t *layout) {
    if (!layout) return;

    for (int i = 0; i < layout->base.child_count; i++) {
        if (layout->base.children[i] && layout->base.children[i]->vtable)
            layout->base.children[i]->vtable->destroy(layout->base.children[i]);
    }

    layout->base.child_count = 0;
}
