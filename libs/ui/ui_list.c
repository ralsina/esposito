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
    widget->selected_fg = TEXT_COLOR_WHITE;
    widget->selected_bg = TEXT_COLOR_BLUE;
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
                           void (*on_selection_changed)(ui_list_widget_t *widget, int new_selection),
                           void (*on_item_selected)(ui_list_widget_t *widget, int item_index),
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
    if (!widget || !widget->items) {
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
        if (widget->user_data) {  // Only call if user_data is valid
            widget->on_selection_changed(widget, widget->selected);
        }
    }
}

int ui_list_get_selection(const ui_list_widget_t *widget) {
    return widget ? widget->selected : -1;
}

void ui_list_scroll_to_item(ui_list_widget_t *widget, int index) {
    if (!widget) {
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
    if (!widget || !widget->draw_scrollbar || widget->count <= widget->visible_rows) {
        return;
    }

    int content_x = widget->x + widget->width - 2; // Inside the border
    int content_y = widget->y + 1; // Below the top border
    int content_height = widget->height - 2; // Excluding borders

    if (content_height < 1) {
        return;
    }

    // Calculate scrollbar position and size using integer math
    int scrollbar_height = (widget->visible_rows * content_height) / widget->count;
    if (scrollbar_height < 1) {
        scrollbar_height = 1;
    }

    int max_position = content_height - scrollbar_height;
    int scrollbar_position = 0;
    if (widget->count > widget->visible_rows) {
        scrollbar_position = (widget->scroll_offset * max_position) / (widget->count - widget->visible_rows);
    }
    if (scrollbar_position < 0) {
        scrollbar_position = 0;
    }
    if (scrollbar_position > max_position) {
        scrollbar_position = max_position;
    }

    // Draw scrollbar track
    for (int i = 0; i < content_height; i++) {
        text_mode_print_at_color(content_x, content_y + i, "│", TEXT_COLOR_BRIGHT_BLACK);
    }

    // Draw scrollbar thumb
    for (int i = 0; i < scrollbar_height; i++) {
        int y_pos = content_y + scrollbar_position + i;
        if (y_pos < content_y + content_height) {
            text_mode_print_at_color(content_x, y_pos, "█", TEXT_COLOR_WHITE);
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

    // Calculate content area (below title)
    int content_x = widget->x + 1; // Inside left border
    int content_y = widget->y + 1; // Inside top border
    int content_width = widget->width - 2; // Excluding borders
    int content_height = widget->height - 2; // Excluding borders

    if (!widget->draw_border) {
        content_x = widget->x;
        content_y = widget->y;
        content_width = widget->width;
        content_height = widget->height;
    }

    // Adjust for title (takes an extra row)
    if (widget->title && widget->draw_border) {
        content_y = widget->y + 2; // Skip title row
        content_height -= 1; // Reduce available height
    }

    if (content_width < 1 || content_height < 1) {
        return;
    }

    int actual_width = content_width;
    if (widget->draw_scrollbar && widget->count > widget->visible_rows) {
        actual_width--; // Reserve space for scrollbar
    }

    // Draw list items
    for (int i = 0; i < content_height && (widget->scroll_offset + i) < widget->count; i++) {
        int item_index = widget->scroll_offset + i;
        const char *item = widget->items[item_index];

        int x = content_x;
        int y = content_y + i;

        bool is_selected = (item_index == widget->selected);

        // Clear the line
        for (int clear_x = 0; clear_x < actual_width; clear_x++) {
            uint8_t fg = is_selected ? widget->selected_fg : widget->normal_fg;
            uint8_t bg = is_selected ? widget->selected_bg : widget->normal_bg;
            text_mode_print_at_attr_bg(x + clear_x, y, " ", fg, bg, TEXT_ATTR_NORMAL);
        }

        if (item) {
            // Truncate item text if too long
            char truncated[64];
            strncpy(truncated, item, sizeof(truncated) - 1);
            truncated[sizeof(truncated) - 1] = '\0';

            if ((int)strlen(truncated) > actual_width) {
                truncated[actual_width] = '\0';
            }

            // Draw item text
            uint8_t fg = is_selected ? TEXT_COLOR_BLACK : widget->normal_fg;
            uint8_t bg = is_selected ? TEXT_COLOR_BRIGHT_GREEN : widget->normal_bg;

            if (is_selected) {
                text_mode_print_at_attr_bg(x, y, "> ", fg, bg, TEXT_ATTR_BOLD);
                text_mode_print_at_attr_bg(x + 2, y, truncated, fg, bg, TEXT_ATTR_NORMAL);
            } else {
                text_mode_print_at_attr_bg(x, y, "  ", fg, bg, TEXT_ATTR_NORMAL);
                text_mode_print_at_attr_bg(x + 2, y, truncated, fg, bg, TEXT_ATTR_NORMAL);
            }
        }
    }

    // Fill remaining space with empty lines
    for (int i = widget->count - widget->scroll_offset; i < content_height; i++) {
        int y = content_y + i;
        for (int clear_x = 0; clear_x < actual_width; clear_x++) {
            text_mode_print_at_attr_bg(content_x + clear_x, y, " ", widget->normal_fg, widget->normal_bg, TEXT_ATTR_NORMAL);
        }
    }

    // Draw scrollbar
    draw_scrollbar(widget);
}

bool ui_list_handle_key(ui_list_widget_t *widget, char key) {
    if (!widget || !widget->enabled || !widget->items) {
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
        if (widget->on_item_selected && widget->user_data) {
            widget->on_item_selected(widget, widget->selected);
        }
        handled = true;
    }

    return handled;
}

bool ui_list_handle_touch(ui_list_widget_t *widget, const event_t *event) {
    if (!widget || !widget->enabled || !widget->visible) {
        return false;
    }

    if (!event || event->type != EVENT_TOUCH || !event->touch.pressed) {
        return false;
    }

    // Get current display rotation and character dimensions
    int rotation = display_get_rotation();
    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();

    // Transform touch coordinates for rotation
    int touch_x = event->touch.x;
    int touch_y = event->touch.y;
    transform_touch_coordinates(&touch_x, &touch_y, rotation);

    // Convert pixel coordinates to character coordinates
    int x_col = touch_x / char_width;
    int y_col = touch_y / char_height;

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

    // Calculate which item was touched
    int item_offset = y_col - content_y;
    int touched_index = widget->scroll_offset + item_offset;

    if (touched_index >= 0 && touched_index < widget->count) {
        ui_list_set_selection(widget, touched_index);

        // Trigger item selection
        if (widget->on_item_selected) {
            widget->on_item_selected(widget, touched_index);
        }

        return true;
    }

    return false;
}