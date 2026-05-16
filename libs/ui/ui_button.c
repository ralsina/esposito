#include "ui_button.h"
#include "os_core.h"
#include <stdlib.h>
#include <string.h>

ui_button_t* ui_button_create(int x, int y, int width, int height, const char *text) {
    ui_button_t *button = (ui_button_t*)malloc(sizeof(ui_button_t));
    if (!button) {
        return NULL;
    }

    button->x = x;
    button->y = y;
    button->width = width;
    button->height = height;
    button->fg_color = TEXT_COLOR_WHITE;
    button->bg_color = TEXT_COLOR_BLUE;
    button->visible = true;
    button->enabled = true;
    button->on_click = NULL;
    button->user_data = NULL;

    if (text) {
        size_t len = strlen(text);
        button->text = (char*)malloc(len + 1);
        if (button->text) {
            memcpy(button->text, text, len + 1);
        } else {
            button->text = NULL;
        }
    } else {
        button->text = NULL;
    }

    return button;
}

void ui_button_destroy(ui_button_t *button) {
    if (!button) {
        return;
    }

    if (button->text) {
        free(button->text);
    }

    free(button);
}

void ui_button_draw(ui_button_t *button) {
    if (!button || !button->visible) {
        return;
    }

    // Draw button background with borders
    for (int dy = 0; dy < button->height; dy++) {
        for (int dx = 0; dx < button->width; dx++) {
            uint8_t attr = TEXT_ATTR_NORMAL;

            // Add borders on the edges
            if (dy == 0) {
                attr |= TEXT_ATTR_BORDER_TOP;
            }
            if (dy == button->height - 1) {
                attr |= TEXT_ATTR_UNDERLINE; // Bottom border
            }
            if (dx == 0) {
                attr |= TEXT_ATTR_BORDER_LEFT;
            }
            if (dx == button->width - 1) {
                attr |= TEXT_ATTR_BORDER_RIGHT;
            }

            text_mode_print_at_attr_bg(button->x + dx, button->y + dy, " ", button->fg_color, button->bg_color, attr);
        }
    }

    // Draw button text centered
    if (button->text) {
        int text_len = strlen(button->text);
        int text_x = button->x + (button->width - text_len) / 2;
        int text_y = button->y + (button->height - 1) / 2;

        if (text_x >= button->x && text_y >= button->y) {
            // Don't draw borders on the text itself
            text_mode_print_at_attr_bg(text_x, text_y, button->text, button->fg_color, button->bg_color, TEXT_ATTR_NORMAL);
        }
    }
}

bool ui_button_handle_touch(ui_button_t *button, const event_t *event) {
    if (!button || !button->enabled || !button->visible) {
        return false;
    }

    if (!event || event->type != EVENT_TOUCH) {
        return false;
    }

    if (!event->touch.pressed) {
        return false; // Only handle press, not release
    }

    // Check if touch is within button bounds
    if (event->touch.x >= button->x && event->touch.x < button->x + button->width &&
        event->touch.y >= button->y && event->touch.y < button->y + button->height) {

        // Trigger callback
        if (button->on_click) {
            button->on_click(button, button->user_data);
        }
        return true;
    }

    return false;
}

void ui_button_set_callback(ui_button_t *button, void (*on_click)(ui_button_t *button, void *user_data), void *user_data) {
    if (!button) {
        return;
    }

    button->on_click = on_click;
    button->user_data = user_data;
}

void ui_button_set_colors(ui_button_t *button, uint16_t fg, uint16_t bg) {
    if (!button) {
        return;
    }

    button->fg_color = fg;
    button->bg_color = bg;
}

void ui_button_set_enabled(ui_button_t *button, bool enabled) {
    if (!button) {
        return;
    }

    button->enabled = enabled;
}

void ui_button_set_visible(ui_button_t *button, bool visible) {
    if (!button) {
        return;
    }

    button->visible = visible;
}