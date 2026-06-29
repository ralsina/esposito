#include "ui2_screen.h"
#include "ui2_graphical.h"
#include "text_mode.h"
#include "hardware.h"
#include <stdlib.h>
#include <string.h>

#define UI2_MAX_FOCUSABLE 64
#define UI2_MAX_SHORTCUTS 16
#define UI2_TAB_KEY 0x09

typedef struct {
    char key;
    uint8_t modifiers;
    ui2_widget_t *target;
} ui2_shortcut_entry_t;

struct ui2_screen_s {
    ui2_layout_t *root;
    ui2_widget_t *focused;
    bool dirty;
    char focus_next_key;
    uint8_t focus_next_mods;
    char focus_prev_key;
    uint8_t focus_prev_mods;
    ui2_shortcut_entry_t shortcuts[UI2_MAX_SHORTCUTS];
    int shortcut_count;

    char toast_msg[64];
    uint8_t toast_fg;
    uint8_t toast_bg;
    int toast_ticks;
};

static int collect_focusable(ui2_widget_t *widget, ui2_widget_t **arr, int max, int index) {
    if (!widget || !widget->visible) return index;
    if (widget->focusable && widget->enabled) {
        if (index < max) arr[index] = widget;
        index++;
    }
    for (int i = 0; i < widget->child_count; i++) {
        index = collect_focusable(widget->children[i], arr, max, index);
    }
    return index;
}

static ui2_widget_t *find_next_focusable(ui2_widget_t *root, ui2_widget_t *current, int direction) {
    ui2_widget_t *arr[UI2_MAX_FOCUSABLE];
    int count = collect_focusable(root, arr, UI2_MAX_FOCUSABLE, 0);
    if (count == 0) return NULL;

    if (!current) return arr[0];

    for (int i = 0; i < count; i++) {
        if (arr[i] == current) {
            int next = (i + direction + count) % count;
            return arr[next];
        }
    }

    return arr[0];
}

static bool handle_touch_recursive(ui2_widget_t *widget, int col, int row, bool pressed) {
    if (!widget || !widget->visible || !widget->enabled) return false;

    for (int i = widget->child_count - 1; i >= 0; i--) {
        if (handle_touch_recursive(widget->children[i], col, row, pressed))
            return true;
    }

    if (col >= widget->x && col < widget->x + widget->width &&
        row >= widget->y && row < widget->y + widget->height) {
        if (widget->vtable->handle_touch(widget, col, row, pressed))
            return true;
    }

    return false;
}

ui2_screen_t *ui2_screen_create(void) {
    ui2_screen_t *screen = (ui2_screen_t *)calloc(1, sizeof(ui2_screen_t));
    if (!screen) return NULL;

    screen->root = NULL;
    screen->focused = NULL;
    screen->focus_next_key = UI2_TAB_KEY;
    screen->focus_next_mods = 0;
    screen->focus_prev_key = UI2_TAB_KEY;
    screen->focus_prev_mods = 1;
    screen->shortcut_count = 0;

    return screen;
}

void ui2_screen_destroy(ui2_screen_t *screen) {
    if (!screen) return;
    if (screen->root) {
        ui2_widget_t *root_w = UI2_WIDGET(screen->root);
        if (root_w->vtable && root_w->vtable->destroy)
            root_w->vtable->destroy(root_w);
    }
    free(screen);
}

void ui2_screen_set_root(ui2_screen_t *screen, ui2_layout_t *root) {
    if (!screen) return;
    if (screen->root) {
        ui2_widget_t *old_root = UI2_WIDGET(screen->root);
        if (old_root->vtable && old_root->vtable->destroy)
            old_root->vtable->destroy(old_root);
    }
    screen->root = root;
    screen->focused = NULL;
    screen->dirty = true;
}

void ui2_screen_shortcut_add(ui2_screen_t *screen, char key, uint8_t modifiers, ui2_widget_t *target) {
    if (!screen || screen->shortcut_count >= UI2_MAX_SHORTCUTS) return;

    screen->shortcuts[screen->shortcut_count].key = key;
    screen->shortcuts[screen->shortcut_count].modifiers = modifiers;
    screen->shortcuts[screen->shortcut_count].target = target;
    screen->shortcut_count++;
}

void ui2_screen_shortcut_remove(ui2_screen_t *screen, char key, uint8_t modifiers) {
    if (!screen) return;

    for (int i = 0; i < screen->shortcut_count; i++) {
        if (screen->shortcuts[i].key == key &&
            screen->shortcuts[i].modifiers == modifiers) {
            for (int j = i; j < screen->shortcut_count - 1; j++)
                screen->shortcuts[j] = screen->shortcuts[j + 1];
            screen->shortcut_count--;
            return;
        }
    }
}

void ui2_screen_set_focus_keys(ui2_screen_t *screen,
                                char next_key, uint8_t next_mods,
                                char prev_key, uint8_t prev_mods) {
    if (!screen) return;
    screen->focus_next_key = next_key;
    screen->focus_next_mods = next_mods;
    screen->focus_prev_key = prev_key;
    screen->focus_prev_mods = prev_mods;
}

void ui2_screen_focus_set(ui2_screen_t *screen, ui2_widget_t *widget) {
    if (!screen) return;

    if (screen->focused && screen->focused->vtable->on_focus)
        screen->focused->vtable->on_focus(screen->focused, false);

    screen->focused = widget;

    if (widget && widget->vtable->on_focus)
        widget->vtable->on_focus(widget, true);
}

void ui2_screen_focus_next(ui2_screen_t *screen) {
    if (!screen || !screen->root) return;
    ui2_widget_t *root_w = UI2_WIDGET(screen->root);
    ui2_widget_t *next = find_next_focusable(root_w, screen->focused, 1);
    if (next) ui2_screen_focus_set(screen, next);
}

void ui2_screen_focus_prev(ui2_screen_t *screen) {
    if (!screen || !screen->root) return;
    ui2_widget_t *root_w = UI2_WIDGET(screen->root);
    ui2_widget_t *prev = find_next_focusable(root_w, screen->focused, -1);
    if (prev) ui2_screen_focus_set(screen, prev);
}

ui2_widget_t *ui2_screen_focus_get(ui2_screen_t *screen) {
    return screen ? screen->focused : NULL;
}

bool ui2_screen_handle_event(ui2_screen_t *screen, event_t *event) {
    if (!screen) return false;

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        uint8_t mods = event->keyboard.modifiers;

        for (int i = 0; i < screen->shortcut_count; i++) {
            if (screen->shortcuts[i].key == key &&
                screen->shortcuts[i].modifiers == mods) {
                ui2_screen_focus_set(screen, screen->shortcuts[i].target);
                return true;
            }
        }

        if (key == screen->focus_next_key && mods == screen->focus_next_mods) {
            ui2_screen_focus_next(screen);
            return true;
        }

        if (key == screen->focus_prev_key && mods == screen->focus_prev_mods) {
            ui2_screen_focus_prev(screen);
            return true;
        }

        if (screen->focused && screen->focused->vtable->handle_key) {
            if (screen->focused->vtable->handle_key(screen->focused, key))
                return true;
        }
    }

    if (event->type == EVENT_TOUCH) {
        int col = event->touch.x / text_mode_get_char_width();
        int row = event->touch.y / text_mode_get_char_height();

        if (screen->root) {
            return handle_touch_recursive(UI2_WIDGET(screen->root), col, row, event->touch.pressed);
        }
    }

    return false;
}

void ui2_screen_toast_show(ui2_screen_t *screen, const char *msg, uint8_t fg, uint8_t bg, int duration_ticks) {
    if (!screen || !msg) return;
    strncpy(screen->toast_msg, msg, sizeof(screen->toast_msg) - 1);
    screen->toast_msg[sizeof(screen->toast_msg) - 1] = '\0';
    screen->toast_fg = fg;
    screen->toast_bg = bg;
    screen->toast_ticks = duration_ticks;
}

void ui2_screen_toast_tick(ui2_screen_t *screen) {
    if (!screen || screen->toast_ticks <= 0) return;
    screen->toast_ticks--;
    if (screen->toast_ticks <= 0) {
        screen->toast_msg[0] = '\0';
        screen->dirty = true;
    }
}

bool ui2_screen_toast_active(ui2_screen_t *screen) {
    return screen && screen->toast_ticks > 0 && screen->toast_msg[0] != '\0';
}

void ui2_screen_render(ui2_screen_t *screen) {
    if (!screen || !screen->root) return;

    if (ui2_is_graphical()) {
        int w = display_get_width();
        int h = display_get_height();
        display_fill_rect(0, 0, w, h, 0x0000);
    } else if (screen->dirty) {
        text_mode_clear(TEXT_COLOR_BLACK);
        screen->dirty = false;
    }

    ui2_widget_t *root_w = UI2_WIDGET(screen->root);
    root_w->vtable->draw(root_w);

    if (screen->toast_ticks > 0 && screen->toast_msg[0]) {
        if (ui2_is_graphical()) {
            int w = display_get_width();
            int h = display_get_height();
            int cw = text_mode_get_char_width();
            int ch = text_mode_get_char_height();
            int msglen = strlen(screen->toast_msg);
            int pw = msglen * cw;
            int px = (w - pw) / 2;
            if (px < 0) px = 0;
            uint16_t bg = ui2_graphical_color(screen->toast_bg);
            uint16_t fg = ui2_graphical_color(screen->toast_fg);
            display_fill_rect(0, 0, w, ch, bg);
            display_draw_text(px, 0, screen->toast_msg, fg);
        } else {
            int cols = text_mode_get_cols();
            int msglen = strlen(screen->toast_msg);
            int x = (cols - msglen) / 2;
            if (x < 0) x = 0;
            for (int cx = 0; cx < cols; cx++) {
                text_mode_print_at_attr_bg(cx, 0, " ", screen->toast_fg, screen->toast_bg, TEXT_ATTR_NORMAL);
            }
            text_mode_print_at_attr_bg(x, 0, screen->toast_msg, screen->toast_fg, screen->toast_bg, TEXT_ATTR_BOLD);
        }
    }

    text_mode_flush();
}
