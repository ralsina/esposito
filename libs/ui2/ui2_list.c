#include "ui2_list.h"
#include <stdlib.h>
#include <string.h>

static void adjust_scroll(ui2_list_t *list) {
    if (list->count <= 0) {
        list->scroll_offset = 0;
        return;
    }
    if (list->selected < list->scroll_offset)
        list->scroll_offset = list->selected;
    if (list->selected >= list->scroll_offset + list->visible_rows)
        list->scroll_offset = list->selected - list->visible_rows + 1;
    if (list->scroll_offset < 0) list->scroll_offset = 0;
    if (list->scroll_offset > list->count - list->visible_rows && list->count > list->visible_rows)
        list->scroll_offset = list->count - list->visible_rows;
    if (list->scroll_offset < 0) list->scroll_offset = 0;
}

static uint8_t border_attr_for_cell(int local_x, int local_y, int width, int height) {
    uint8_t attr = TEXT_ATTR_NORMAL;
    if (local_y == 0) attr |= TEXT_ATTR_BORDER_TOP;
    if (local_y == height - 1) attr |= TEXT_ATTR_UNDERLINE;
    if (local_x == 0) attr |= TEXT_ATTR_BORDER_LEFT;
    if (local_x == width - 1) attr |= TEXT_ATTR_BORDER_RIGHT;
    return attr;
}

static void ui2_list_draw(ui2_widget_t *widget) {
    ui2_list_t *list = (ui2_list_t *)widget;
    if (!widget->visible) return;

    if (list->border) {
        for (int dy = 0; dy < widget->height; dy++) {
            for (int dx = 0; dx < widget->width; dx++) {
                text_mode_print_at_attr_bg(widget->x + dx, widget->y + dy, " ",
                                           list->border_fg, list->normal_bg,
                                           border_attr_for_cell(dx, dy, widget->width, widget->height));
            }
        }
    }

    if (!list->items || list->count <= 0) return;

    int content_y = widget->y;
    int scrollbar_width = (list->count > list->visible_rows) ? list->scrollbar_width : 0;
    int content_width = widget->width - scrollbar_width;

    if (list->title) {
        int title_x = widget->x + (content_width - (int)strlen(list->title)) / 2;
        if (title_x < widget->x) title_x = widget->x;
        text_mode_print_at_attr(title_x, content_y, list->title,
                                TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
        content_y++;
    }

    for (int i = 0; i < list->visible_rows && (list->scroll_offset + i) < list->count; i++) {
        int index = list->scroll_offset + i;
        int y = content_y + i;

        bool is_selected = (index == list->selected);

        uint8_t fg = is_selected ? list->selected_fg : list->normal_fg;
        uint8_t bg = is_selected ? list->selected_bg : list->normal_bg;
        uint8_t edge_fg = list->border_fg;

        for (int cx = 0; cx < content_width; cx++) {
            uint8_t cell_fg = (cx == 0 || cx == content_width - 1) ? edge_fg : fg;
            uint8_t attr = border_attr_for_cell(cx, y - widget->y, widget->width, widget->height);
            text_mode_print_at_attr_bg(widget->x + cx, y, " ", cell_fg, bg, attr);
        }

        if (list->items[index]) {
            char truncated[64];
            strncpy(truncated, list->items[index], sizeof(truncated) - 1);
            truncated[sizeof(truncated) - 1] = '\0';

            int max_text = content_width - 3;
            if (max_text < 0) max_text = 0;
            if ((int)strlen(truncated) > max_text)
                truncated[max_text] = '\0';

            uint8_t row_attr = TEXT_ATTR_NORMAL;
            if (list->row_attrs && index < list->row_attrs_count)
                row_attr = list->row_attrs[index];

            uint8_t marker_border = border_attr_for_cell(0, y - widget->y, widget->width, widget->height);
            uint8_t marker_attr = (is_selected ? TEXT_ATTR_BOLD : row_attr) | marker_border;
            text_mode_print_at_attr_bg(widget->x, y, is_selected ? ">" : " ", edge_fg, bg, marker_attr);
            text_mode_print_at_attr_bg(widget->x + 1, y, " ", fg, bg, is_selected ? TEXT_ATTR_BOLD : row_attr);

            uint8_t text_attr = is_selected ? TEXT_ATTR_BOLD : row_attr;
            text_mode_print_at_attr_bg(widget->x + 2, y, truncated, fg, bg, text_attr);
        }
    }

    if (scrollbar_width > 0) {
        int sb_x = widget->x + widget->width - 1;
        int sb_rows = list->visible_rows;

        int thumb_size = (sb_rows * sb_rows) / list->count;
        if (thumb_size < 1) thumb_size = 1;
        if (thumb_size > sb_rows) thumb_size = sb_rows;

        int max_offset = list->count - list->visible_rows;
        int thumb_start = 0;
        if (max_offset > 0) {
            thumb_start = (list->scroll_offset * (sb_rows - thumb_size)) / max_offset;
        }
        if (thumb_start > sb_rows - thumb_size)
            thumb_start = sb_rows - thumb_size;

        for (int i = 0; i < sb_rows; i++) {
            int y = content_y + i;
            if (i >= thumb_start && i < thumb_start + thumb_size) {
                text_mode_print_at_attr_bg(sb_x, y, " ",
                    list->selected_fg, list->selected_bg, TEXT_ATTR_BOLD);
            } else {
                text_mode_print_at_attr_bg(sb_x, y, " ",
                    list->border_fg, list->normal_bg, TEXT_ATTR_BORDER_LEFT);
            }
        }
    }
}

static bool ui2_list_handle_key(ui2_widget_t *widget, char key) {
    ui2_list_t *list = (ui2_list_t *)widget;
    if (!widget->enabled || !list->items || list->count <= 0) return false;

    if (key == 'w' || key == 'W') {
        if (list->selected > 0) {
            int old = list->selected;
            list->selected--;
            adjust_scroll(list);
            if (old != list->selected && list->on_selection_changed)
                list->on_selection_changed(list->selected, list->cb_data);
        }
        return true;
    }

    if (key == 's' || key == 'S') {
        if (list->selected < list->count - 1) {
            int old = list->selected;
            list->selected++;
            adjust_scroll(list);
            if (old != list->selected && list->on_selection_changed)
                list->on_selection_changed(list->selected, list->cb_data);
        }
        return true;
    }

    if (key == '\n' || key == '\r') {
        if (list->on_item_activated)
            list->on_item_activated(list->selected, list->cb_data);
        return true;
    }

    return false;
}

static bool ui2_list_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed) {
    ui2_list_t *list = (ui2_list_t *)widget;
    if (!widget->enabled || !widget->visible || !pressed) return false;
    if (!list->items || list->count <= 0) return false;

    int content_y = widget->y;
    if (list->title) content_y++;

    int scrollbar_width = (list->count > list->visible_rows) ? list->scrollbar_width : 0;

    if (scrollbar_width > 0 && col >= widget->x + widget->width - scrollbar_width) {
        if (row >= content_y && row < content_y + list->visible_rows) {
            int max_offset = list->count - list->visible_rows;
            if (max_offset > 0) {
                int midpoint = content_y + list->visible_rows / 2;
                int new_offset = list->scroll_offset;
                if (row < midpoint) {
                    new_offset -= list->visible_rows;
                    if (new_offset < 0) new_offset = 0;
                } else {
                    new_offset += list->visible_rows;
                    if (new_offset > max_offset) new_offset = max_offset;
                }
                if (new_offset != list->scroll_offset) {
                    int old_sel = list->selected;
                    list->scroll_offset = new_offset;
                    if (list->selected < list->scroll_offset)
                        list->selected = list->scroll_offset;
                    if (list->selected >= list->scroll_offset + list->visible_rows)
                        list->selected = list->scroll_offset + list->visible_rows - 1;
                    if (list->selected >= list->count) list->selected = list->count - 1;
                    if (old_sel != list->selected && list->on_selection_changed)
                        list->on_selection_changed(list->selected, list->cb_data);
                }
            }
            return true;
        }
        return false;
    }

    if (row < content_y) return false;

    int item_offset = row - content_y;
    int touched_index = list->scroll_offset + item_offset;

    if (touched_index >= 0 && touched_index < list->count) {
        int old = list->selected;
        list->selected = touched_index;
        if (old != list->selected && list->on_selection_changed)
            list->on_selection_changed(list->selected, list->cb_data);
        if (list->on_item_activated)
            list->on_item_activated(list->selected, list->cb_data);
        return true;
    }

    return false;
}

static void ui2_list_on_focus(ui2_widget_t *widget, bool focused) {
    ui2_list_t *list = (ui2_list_t *)widget;
    if (focused) {
        list->selected_fg = TEXT_COLOR_BLACK;
        list->selected_bg = TEXT_COLOR_BRIGHT_GREEN;
        list->border_fg = TEXT_COLOR_BRIGHT_WHITE;
    } else {
        list->selected_fg = TEXT_COLOR_BRIGHT_WHITE;
        list->selected_bg = TEXT_COLOR_GREEN;
        list->border_fg = TEXT_COLOR_BRIGHT_BLACK;
    }
}

static const ui2_widget_vtable_t list_vtable = {
    .draw = ui2_list_draw,
    .handle_key = ui2_list_handle_key,
    .handle_touch = ui2_list_handle_touch,
    .on_focus = ui2_list_on_focus,
    .destroy = ui2_list_destroy
};

ui2_list_t *ui2_list_create(int x, int y, int width, int height) {
    ui2_list_t *list = (ui2_list_t *)calloc(1, sizeof(ui2_list_t));
    if (!list) return NULL;

    list->base.vtable = &list_vtable;
    list->base.x = x;
    list->base.y = y;
    list->base.width = width;
    list->base.height = height;
    list->base.visible = true;
    list->base.enabled = true;
    list->base.focusable = true;
    list->base.children = NULL;
    list->base.child_count = 0;
    list->base.user_data = NULL;

    list->items = NULL;
    list->count = 0;
    list->selected = 0;
    list->scroll_offset = 0;
    list->visible_rows = height - 1;
    list->title = NULL;
    list->normal_fg = TEXT_COLOR_WHITE;
    list->normal_bg = TEXT_COLOR_BLACK;
    list->selected_fg = TEXT_COLOR_BRIGHT_WHITE;
    list->selected_bg = TEXT_COLOR_GREEN;
    list->border_fg = TEXT_COLOR_CYAN;
    list->unfocused_border_fg = TEXT_COLOR_CYAN;
    list->scrollbar_width = 1;
    list->border = false;
    list->row_attrs = NULL;
    list->row_attrs_count = 0;
    list->on_selection_changed = NULL;
    list->on_item_activated = NULL;
    list->cb_data = NULL;

    return list;
}

void ui2_list_destroy(ui2_widget_t *widget) {
    if (!widget) return;
    ui2_list_t *list = (ui2_list_t *)widget;
    free(list->title);
    free(list);
}

void ui2_list_set_items(ui2_list_t *list, const char **items, int count) {
    if (!list) return;
    list->items = items;
    list->count = count;
    list->selected = 0;
    list->scroll_offset = 0;
}

void ui2_list_set_title(ui2_list_t *list, const char *title) {
    if (!list) return;
    free(list->title);
    list->title = NULL;
    if (title) {
        size_t len = strlen(title);
        list->title = (char *)malloc(len + 1);
        if (list->title) memcpy(list->title, title, len + 1);
    }
}

void ui2_list_set_selection(ui2_list_t *list, int index) {
    if (!list || list->count <= 0) return;
    if (index < 0) index = 0;
    if (index >= list->count) index = list->count - 1;
    int old = list->selected;
    list->selected = index;
    adjust_scroll(list);
    if (old != list->selected && list->on_selection_changed)
        list->on_selection_changed(list->selected, list->cb_data);
}

int ui2_list_get_selection(const ui2_list_t *list) {
    return list ? list->selected : -1;
}

void ui2_list_set_callbacks(ui2_list_t *list,
                             ui2_list_selection_cb on_changed,
                             ui2_list_activate_cb on_activated,
                             void *user_data) {
    if (!list) return;
    list->on_selection_changed = on_changed;
    list->on_item_activated = on_activated;
    list->cb_data = user_data;
}

void ui2_list_set_colors(ui2_list_t *list, uint8_t normal_fg, uint8_t normal_bg,
                          uint8_t selected_fg, uint8_t selected_bg, uint8_t border_fg) {
    if (!list) return;
    list->normal_fg = normal_fg;
    list->normal_bg = normal_bg;
    list->selected_fg = selected_fg;
    list->selected_bg = selected_bg;
    list->border_fg = border_fg;
    list->unfocused_border_fg = border_fg;
}

void ui2_list_set_scrollbar_width(ui2_list_t *list, int width) {
    if (!list) return;
    list->scrollbar_width = (width > 0) ? width : 0;
}

void ui2_list_set_border(ui2_list_t *list, bool enabled) {
    if (!list) return;
    list->border = enabled;
}

void ui2_list_set_row_attrs(ui2_list_t *list, const uint8_t *attrs, int count) {
    if (!list) return;
    list->row_attrs = attrs;
    list->row_attrs_count = count;
}
