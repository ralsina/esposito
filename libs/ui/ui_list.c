#include "ui_list.h"
#include "os_core.h"
#include "hardware.h"
#include <stdlib.h>
#include <string.h>

ui_list_widget_t* ui_list_create(int x, int y, int width, int height) {
    ui_list_widget_t *widget = (ui_list_widget_t*)malloc(sizeof(struct ui_list_widget_t));
    if (!widget) {
        return NULL;
    }

    widget->items = NULL;
    widget->count = 0;
    widget->selected = 0;
    widget->scroll_offset = 0;
    widget->visible_rows = height - 2; // Account for top/bottom borders if drawn

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    widget->title = NULL;

    // Default colors
    widget->normal_fg = TEXT_COLOR_WHITE;
    widget->normal_bg = TEXT_COLOR_BLACK;
    widget->selected_fg = TEXT_COLOR_BRIGHT_WHITE;
    widget->selected_bg = TEXT_COLOR_GREEN;
    widget->border_fg = TEXT_COLOR_CYAN;
    widget->title_fg = TEXT_COLOR_BRIGHT_CYAN;
    widget->title_bg = TEXT_COLOR_BLACK;

    widget->visible = true;
    widget->enabled = true;
    widget->draw_border = true;
    widget->draw_scrollbar = true;

    widget->on_selection_changed = NULL;
    widget->on_item_selected = NULL;
    widget->user_data = NULL;

    return widget;
}

void ui_list_destroy(ui_list_widget_t *widget) {
    if (!widget) {
        return;
    }
    if (widget->title) {
        free(widget->title);
    }
    free(widget);
}

void ui_list_set_title(ui_list_widget_t *widget, const char *title) {
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

void ui_list_set_items(ui_list_widget_t *widget, const char **items, int count) {
    if (!widget) {
        return;
    }

    widget->items = items;
    widget->count = count;

    // Reset selection and scroll
    widget->selected = 0;
    widget->scroll_offset = 0;

    // Recalculate visible rows
    widget->visible_rows = widget->height - 2;
    if (!widget->draw_border) {
        widget->visible_rows = widget->height;
    }
}

void ui_list_set_colors(ui_list_widget_t *widget, uint8_t normal_fg, uint8_t normal_bg,
                        uint8_t selected_fg, uint8_t selected_bg, uint8_t border_fg) {
    if (!widget) {
        return;
    }

    widget->normal_fg = normal_fg;
    widget->normal_bg = normal_bg;
    widget->selected_fg = selected_fg;
    widget->selected_bg = selected_bg;
    widget->border_fg = border_fg;
}

void ui_list_set_callbacks(ui_list_widget_t *widget,
                           ui_list_selection_changed_cb on_selection_changed,
                           ui_list_item_selected_cb on_item_selected,
                           void *user_data) {
    if (!widget) {
        return;
    }

    widget->on_selection_changed = on_selection_changed;
    widget->on_item_selected = on_item_selected;
    widget->user_data = user_data;
}

void ui_list_set_border(ui_list_widget_t *widget, bool draw_border) {
    if (!widget) {
        return;
    }
    widget->draw_border = draw_border;

    // Recalculate visible rows
    widget->visible_rows = widget->height - 2;
    if (!widget->draw_border) {
        widget->visible_rows = widget->height;
    }
}

void ui_list_set_scrollbar(ui_list_widget_t *widget, bool draw_scrollbar) {
    if (!widget) {
        return;
    }
    widget->draw_scrollbar = draw_scrollbar;
}

void ui_list_set_selection(ui_list_widget_t *widget, int index) {
    if (!widget || !widget->items || widget->count <= 0) {
        return;
    }

    if (index < 0) {
        index = 0;
    } else if (index >= widget->count) {
        index = widget->count - 1;
    }

    int old_selection = widget->selected;
    widget->selected = index;

    // Adjust scroll offset to keep selection visible
    if (widget->selected < widget->scroll_offset) {
        widget->scroll_offset = widget->selected;
    } else if (widget->selected >= widget->scroll_offset + widget->visible_rows) {
        widget->scroll_offset = widget->selected - widget->visible_rows + 1;
    }

    // Trigger callback if selection changed
    if (old_selection != widget->selected && widget->on_selection_changed) {
        widget->on_selection_changed(widget, widget->selected, widget->user_data);
    }
}

int ui_list_get_selection(const ui_list_widget_t *widget) {
    return widget ? widget->selected : -1;
}

void ui_list_scroll_to_item(ui_list_widget_t *widget, int index) {
    if (!widget || widget->count <= 0) {
        return;
    }

    if (index < 0) {
        index = 0;
    } else if (index >= widget->count) {
        index = widget->count - 1;
    }

    widget->scroll_offset = index;
    if (widget->scroll_offset + widget->visible_rows > widget->count) {
        widget->scroll_offset = widget->count - widget->visible_rows;
    }
    if (widget->scroll_offset < 0) {
        widget->scroll_offset = 0;
    }
}

static void draw_list_border(const ui_list_widget_t *widget) {
    if (!widget || !widget->draw_border) {
        return;
    }

    int x = widget->x;
    int y = widget->y;
    int w = widget->width;
    int h = widget->height;

    // Draw corners and borders using text mode border attributes
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            uint8_t attr = TEXT_ATTR_NORMAL;

            // Add borders on the edges
            if (dy == 0) {
                attr |= TEXT_ATTR_BORDER_TOP;
            }
            if (dy == h - 1) {
                attr |= TEXT_ATTR_UNDERLINE;
            }
            if (dx == 0) {
                attr |= TEXT_ATTR_BORDER_LEFT;
            }
            if (dx == w - 1) {
                attr |= TEXT_ATTR_BORDER_RIGHT;
            }

            // Only draw borders, not the content area
            if (dy == 0 || dy == h - 1 || dx == 0 || dx == w - 1) {
                text_mode_print_at_attr_bg(x + dx, y + dy, " ", widget->border_fg, widget->normal_bg, attr);
            }
        }
    }
}

static void draw_scrollbar(const ui_list_widget_t *widget) {
    if (!widget || !widget->draw_scrollbar || widget->count <= widget->visible_rows || widget->count <= 0) {
        return;
    }

    int content_x = widget->x + widget->width - 1; // On the right border
    int content_y = widget->y + 1; // Inside top border
    int content_height = widget->height - 2; // Excluding top/bottom borders

    if (content_height < 1) {
        return;
    }

    // Calculate scrollbar position and size using integer math
    int thumb_height = (widget->visible_rows * content_height) / widget->count;
    if (thumb_height < 1) {
        thumb_height = 1;
    }

    int max_position = content_height - thumb_height;
    int thumb_position = 0;
    if (widget->count > widget->visible_rows) {
        thumb_position = (widget->scroll_offset * max_position) / (widget->count - widget->visible_rows);
    }

    // Ensure thumb doesn't overflow
    if (thumb_position < 0) thumb_position = 0;
    if (thumb_position > max_position) thumb_position = max_position;

    // Draw scrollbar on the right border column
    for (int i = 0; i < content_height; i++) {
        int y_pos = content_y + i;
        uint8_t attr = TEXT_ATTR_BORDER_RIGHT | TEXT_ATTR_BORDER_LEFT;
        bool is_thumb = (i >= thumb_position && i < thumb_position + thumb_height);

        if (is_thumb) {
            // Thumb is a reverse space (white on cyan border color)
            text_mode_print_at_attr_bg(content_x, y_pos, " ", widget->normal_bg, widget->border_fg, attr);
        } else {
            // Track is a normal space with border attribute
            text_mode_print_at_attr_bg(content_x, y_pos, " ", widget->border_fg, widget->normal_bg, attr);
        }
    }
}

void ui_list_draw(const ui_list_widget_t *widget) {
    if (!widget || !widget->visible || !widget->items) {
        return;
    }

    // Draw border if enabled
    draw_list_border(widget);

    // Draw title if present
    if (widget->title) {
        int title_x = widget->x + 2;
        int title_y = widget->y;

        // Draw title background (top border with corners)
        for (int x = widget->x; x < widget->x + widget->width; x++) {
            uint8_t attr = TEXT_ATTR_BORDER_TOP;
            if (x == widget->x) {
                attr |= TEXT_ATTR_BORDER_LEFT;
            } else if (x == widget->x + widget->width - 1) {
                attr |= TEXT_ATTR_BORDER_RIGHT;
            }
            text_mode_print_at_attr_bg(x, title_y, " ", widget->border_fg, widget->title_bg, attr);
        }

        // Draw space padding before title
        text_mode_print_at_attr_bg(title_x, title_y, " ", widget->title_bg, widget->title_bg, TEXT_ATTR_NORMAL);

        // Draw title text
        text_mode_print_at_attr_bg(title_x + 1, title_y, widget->title, widget->title_fg, widget->title_bg, TEXT_ATTR_NORMAL);

        // Draw space padding after title
        int title_len = strlen(widget->title);
        text_mode_print_at_attr_bg(title_x + 1 + title_len, title_y, " ", widget->title_bg, widget->title_bg, TEXT_ATTR_NORMAL);
    }

    // Calculate content area
    int content_x = widget->x + 1; // Inside left border
    int content_y = widget->y + 1; // Inside top border
    int content_width = widget->width - 2; // Excluding borders

    if (!widget->draw_border) {
        content_x = widget->x;
        content_y = widget->y;
        content_width = widget->width;
    }

    // Adjust for title
    if (widget->title && widget->draw_border) {
        content_y = widget->y + 2; // Skip title row
    }

    if (content_width < 1 || widget->visible_rows < 1) {
        return;
    }

    // Draw list items - use visible_rows instead of content_height to avoid overflow
    for (int i = 0; i < widget->visible_rows && (widget->scroll_offset + i) < widget->count; i++) {
        int item_index = widget->scroll_offset + i;
        const char *item = widget->items[item_index];

        int x = content_x;
        int y = content_y + i;

        bool is_selected = (item_index == widget->selected);

        uint8_t fg = is_selected ? widget->selected_fg : widget->normal_fg;
        uint8_t bg = is_selected ? widget->selected_bg : widget->normal_bg;

        // Check if this is the last row of the widget to preserve the box
        uint8_t attr = TEXT_ATTR_NORMAL;
        if (widget->draw_border && y == widget->y + widget->height - 1) {
            attr |= TEXT_ATTR_UNDERLINE;
        }

        // Clear the line
        for (int clear_x = 0; clear_x < content_width; clear_x++) {
            text_mode_print_at_attr_bg(x + clear_x, y, " ", fg, bg, attr);
        }

        if (item) {
            // Truncate item text if too long
            char truncated[64];
            strncpy(truncated, item, sizeof(truncated) - 1);
            truncated[sizeof(truncated) - 1] = '\0';

            if ((int)strlen(truncated) > content_width - 2) {
                truncated[content_width - 2] = '\0';
            }

            // Draw selection marker and text
            if (is_selected) {
                text_mode_print_at_attr_bg(x, y, "> ", fg, bg, attr | TEXT_ATTR_BOLD);
                text_mode_print_at_attr_bg(x + 2, y, truncated, fg, bg, attr);
            } else {
                text_mode_print_at_attr_bg(x, y, "  ", fg, bg, attr);
                text_mode_print_at_attr_bg(x + 2, y, truncated, fg, bg, attr);
            }
        }
    }

    // Fill remaining space with empty lines
    for (int i = widget->count - widget->scroll_offset; i < widget->visible_rows; i++) {
        if (i < 0) continue;
        int y = content_y + i;

        uint8_t attr = TEXT_ATTR_NORMAL;
        if (widget->draw_border && y == widget->y + widget->height - 1) {
            attr |= TEXT_ATTR_UNDERLINE;
        }

        for (int clear_x = 0; clear_x < content_width; clear_x++) {
            text_mode_print_at_attr_bg(content_x + clear_x, y, " ", widget->normal_fg, widget->normal_bg, attr);
        }
    }

    // Draw scrollbar
    draw_scrollbar(widget);
}

bool ui_list_handle_key(ui_list_widget_t *widget, char key) {
    if (!widget || !widget->enabled || !widget->items || widget->count <= 0) {
        return false;
    }

    bool handled = false;

    if (key == 'w' || key == 'W') {
        // Move selection up
        if (widget->selected > 0) {
            ui_list_set_selection(widget, widget->selected - 1);
            handled = true;
        }
    } else if (key == 's' || key == 'S') {
        // Move selection down
        if (widget->selected < widget->count - 1) {
            ui_list_set_selection(widget, widget->selected + 1);
            handled = true;
        }
    } else if (key == '\n' || key == '\r') {
        // Select item
        if (widget->on_item_selected) {
            widget->on_item_selected(widget, widget->selected, widget->user_data);
        }
        handled = true;
    }

    return handled;
}

bool ui_list_handle_touch(ui_list_widget_t *widget, const event_t *event) {
    if (!widget || !widget->enabled || !widget->visible || widget->count <= 0) {
        return false;
    }

    if (!event || event->type != EVENT_TOUCH || !event->touch.pressed) {
        return false;
    }

    // Touch coordinates are already transformed for current rotation
    // Convert pixel coordinates to character coordinates
    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();

    int x_col = event->touch.x / char_width;
    int y_col = event->touch.y / char_height;

    // Check if touch is within list bounds
    if (x_col < widget->x || x_col >= widget->x + widget->width ||
        y_col < widget->y || y_col >= widget->y + widget->height) {
        return false;
    }

    // Calculate content area (excluding borders)
    int content_y = widget->y + 1;
    if (!widget->draw_border) {
        content_y = widget->y;
    }

    // Adjust for title (takes an extra row)
    if (widget->title && widget->draw_border) {
        content_y = widget->y + 2; // Skip title row
    }

    // Calculate which item was touched
    int item_offset = y_col - content_y;
    int touched_index = widget->scroll_offset + item_offset;

    if (touched_index >= 0 && touched_index < widget->count) {
        ui_list_set_selection(widget, touched_index);

        // Trigger item selection
        if (widget->on_item_selected) {
            widget->on_item_selected(widget, touched_index, widget->user_data);
        }

        return true;
    }

    return false;
}