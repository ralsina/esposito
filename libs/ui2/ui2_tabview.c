#include "ui2_tabview.h"
#include "ui2_graphical.h"
#include "hardware.h"
#include <stdlib.h>
#include <string.h>

static void ui2_tabview_position_content(ui2_tabview_t *tv) {
    int cx = tv->base.x + tv->left_width + 1;
    int cw = tv->base.x + tv->base.width - cx;
    int cy = tv->base.y;
    int ch = tv->base.height;

    for (int i = 0; i < tv->base.child_count; i++) {
        ui2_widget_t *layout = tv->base.children[i];
        layout->x = cx;
        layout->y = cy;
        layout->width = cw;
        layout->height = ch;
        layout->visible = (i == tv->selected_index);
    }
}

static void ui2_tabview_draw(ui2_widget_t *widget) {
    ui2_tabview_t *tv = (ui2_tabview_t *)widget;
    if (!widget->visible) return;

    if (ui2_is_graphical()) {
        int px = ui2_graphical_px(widget->x);
        int py = ui2_graphical_py(widget->y);
        int pw = ui2_graphical_pw(widget->width);
        int ph = ui2_graphical_ph(widget->height);
        int ch = text_mode_get_char_height();
        int cw = text_mode_get_char_width();
        int lw = tv->left_width * cw;

        display_fill_rect(px, py, pw, ph, 0x0000);

        for (int row = 0; row < widget->height && row < tv->tab_count; row++) {
            int row_py = py + row * ch;
            bool is_sel = (row == tv->selected_index);

            uint16_t fg, bg;
            if (is_sel && tv->focus_on_tabs) {
                fg = 0x0000; bg = 0x07E0;
            } else if (is_sel) {
                fg = 0x0000; bg = 0x0400;
            } else {
                fg = 0xFFFF; bg = 0x0000;
            }

            display_fill_rect(px, row_py, lw, ch, bg);

            if (tv->tab_titles[row]) {
                char label[UI2_TAB_MAX_TITLE];
                strncpy(label, tv->tab_titles[row], UI2_TAB_MAX_TITLE - 1);
                label[UI2_TAB_MAX_TITLE - 1] = '\0';
                int max_cw = tv->left_width - 2;
                if (max_cw < 0) max_cw = 0;
                if ((int)strlen(label) > max_cw)
                    label[max_cw] = '\0';
                display_draw_text(px + cw, row_py, label, fg);
            }
        }

        // Divider line between tab strip and content
        for (int row = 0; row < widget->height; row++) {
            int row_py = py + row * ch;
            display_draw_pixel(px + lw, row_py, 0x8410);
        }

        // Draw tab content children
        for (int i = 0; i < widget->child_count; i++) {
            if (widget->children[i]->visible)
                widget->children[i]->vtable->draw(widget->children[i]);
        }

        // Bottom border
        int bottom_py = py + ph - 1;
        for (int col_px = 0; col_px < pw; col_px++) {
            display_draw_pixel(px + col_px, bottom_py, 0x8410);
        }
        return;
    }

    int x0 = widget->x, y0 = widget->y;

    for (int row = 0; row < widget->height; row++) {
        int gy = y0 + row;

        bool is_sel = (row < tv->tab_count && row == tv->selected_index);
        uint8_t fg, bg;
        if (is_sel && tv->focus_on_tabs) {
            fg = TEXT_COLOR_BLACK;
            bg = TEXT_COLOR_BRIGHT_GREEN;
        } else if (is_sel) {
            fg = TEXT_COLOR_BLACK;
            bg = TEXT_COLOR_GREEN;
        } else {
            fg = TEXT_COLOR_WHITE;
            bg = TEXT_COLOR_BLACK;
        }

        for (int col = 0; col < tv->left_width; col++) {
            int gx = x0 + col;
            uint8_t attr = TEXT_ATTR_NORMAL;
            if (col == 0) attr |= TEXT_ATTR_BORDER_LEFT;
            if (row == 0) attr |= TEXT_ATTR_BORDER_TOP;
            if (row == widget->height - 1) attr |= TEXT_ATTR_UNDERLINE;
            text_mode_print_at_attr_bg(gx, gy, " ", fg, bg, attr);
        }

        if (row < tv->tab_count && tv->tab_titles[row]) {
            char label[UI2_TAB_MAX_TITLE];
            int tx = x0 + 1;

            strncpy(label, tv->tab_titles[row], UI2_TAB_MAX_TITLE - 1);
            label[UI2_TAB_MAX_TITLE - 1] = '\0';
            if ((int)strlen(label) > tv->left_width - 2)
                label[tv->left_width - 2] = '\0';

            uint8_t bold_attr = is_sel ? TEXT_ATTR_BOLD : TEXT_ATTR_NORMAL;
            text_mode_print_at_attr_bg(tx, gy, label, fg, bg, bold_attr);
        }

        uint8_t divider_attr = TEXT_ATTR_NORMAL;
        if (row == 0) divider_attr |= TEXT_ATTR_BORDER_TOP;
        if (row == widget->height - 1) divider_attr |= TEXT_ATTR_UNDERLINE;
        text_mode_print_at_attr_bg(x0 + tv->left_width, gy, " ",
                                   TEXT_COLOR_CYAN, TEXT_COLOR_BLACK,
                                   divider_attr | TEXT_ATTR_BORDER_LEFT);
    }

    for (int i = 0; i < widget->child_count; i++) {
        if (widget->children[i]->visible)
            widget->children[i]->vtable->draw(widget->children[i]);
    }

    int bottom_y = y0 + widget->height - 1;
    for (int col = x0; col < x0 + widget->width; col++) {
        uint8_t ba = TEXT_ATTR_UNDERLINE;
        if (col == x0) ba |= TEXT_ATTR_BORDER_LEFT;
        if (col == x0 + widget->width - 1) ba |= TEXT_ATTR_BORDER_RIGHT;
        text_mode_print_at_attr_bg(col, bottom_y, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, ba);
    }
}

static bool is_key_up(char key) {
    return key == 'w' || key == 'W' || (unsigned char)key == 0x99;
}

static bool is_key_down(char key) {
    return key == 's' || key == 'S' || (unsigned char)key == 0x98;
}

static bool is_key_left(char key) {
    return key == 'a' || key == 'A' || (unsigned char)key == 0x97;
}

static bool is_key_right(char key) {
    return key == 'd' || key == 'D' || (unsigned char)key == 0x96;
}

static bool is_key_confirm(char key) {
    return key == '\n' || key == '\r';
}

static bool is_key_cancel(char key) {
    return key == '\x1b';
}

static ui2_widget_t *selected_content(ui2_tabview_t *tv) {
    if (tv->selected_index >= 0 && tv->selected_index < tv->base.child_count)
        return tv->base.children[tv->selected_index];
    return NULL;
}

static void set_focus_on_tabs(ui2_tabview_t *tv, bool on_tabs) {
    if (tv->focus_on_tabs == on_tabs) return;
    tv->focus_on_tabs = on_tabs;

    ui2_widget_t *content = selected_content(tv);
    if (content && content->vtable && content->vtable->on_focus)
        content->vtable->on_focus(content, !on_tabs);
}

static bool ui2_tabview_handle_key(ui2_widget_t *widget, char key) {
    ui2_tabview_t *tv = (ui2_tabview_t *)widget;
    if (!widget->enabled) return false;

    if (tv->focus_on_tabs) {
        if (is_key_up(key)) {
            if (tv->selected_index > 0) {
                tv->selected_index--;
                ui2_tabview_position_content(tv);
            }
            return true;
        }
        if (is_key_down(key)) {
            if (tv->selected_index < tv->tab_count - 1) {
                tv->selected_index++;
                ui2_tabview_position_content(tv);
            }
            return true;
        }
        if (is_key_right(key) || is_key_confirm(key)) {
            set_focus_on_tabs(tv, false);
            return true;
        }
    } else {
        if (is_key_left(key) || is_key_cancel(key)) {
            set_focus_on_tabs(tv, true);
            return true;
        }

        ui2_widget_t *content = selected_content(tv);
        if (content && content->vtable && content->vtable->handle_key)
            return content->vtable->handle_key(content, key);
    }

    return false;
}

static bool ui2_tabview_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed) {
    ui2_tabview_t *tv = (ui2_tabview_t *)widget;
    if (!widget->enabled || !widget->visible || !pressed) return false;
    if (col < widget->x || col >= widget->x + widget->width) return false;
    if (row < widget->y || row >= widget->y + widget->height) return false;

    if (col < widget->x + tv->left_width) {
        int tab_index = row - widget->y;
        if (tab_index >= 0 && tab_index < tv->tab_count) {
            tv->selected_index = tab_index;
            ui2_tabview_position_content(tv);
            set_focus_on_tabs(tv, true);
            return true;
        }
        return true;
    }

    set_focus_on_tabs(tv, false);
    ui2_widget_t *content = selected_content(tv);
    if (content && content->vtable && content->vtable->handle_touch)
        return content->vtable->handle_touch(content, col, row, pressed);
    return false;
}

static void ui2_tabview_on_focus(ui2_widget_t *widget, bool focused) {
    ui2_tabview_t *tv = (ui2_tabview_t *)widget;
    if (focused) {
        set_focus_on_tabs(tv, true);
    }
}

static void ui2_tabview_destroy(ui2_widget_t *widget) {
    if (!widget) return;
    ui2_tabview_t *tv = (ui2_tabview_t *)widget;

    for (int i = 0; i < widget->child_count; i++) {
        if (widget->children[i] && widget->children[i]->vtable &&
            widget->children[i]->vtable->destroy)
            widget->children[i]->vtable->destroy(widget->children[i]);
    }

    for (int i = 0; i < tv->tab_count; i++)
        free(tv->tab_titles[i]);
    free(tv->tab_titles);
    free(widget->children);
    free(tv);
}

static const ui2_widget_vtable_t tabview_vtable = {
    .draw = ui2_tabview_draw,
    .handle_key = ui2_tabview_handle_key,
    .handle_touch = ui2_tabview_handle_touch,
    .on_focus = ui2_tabview_on_focus,
    .destroy = ui2_tabview_destroy
};

ui2_widget_t *ui2_tabview_create(int x, int y, int width, int height, int left_width) {
    ui2_tabview_t *tv = (ui2_tabview_t *)calloc(1, sizeof(ui2_tabview_t));
    if (!tv) return NULL;

    tv->base.vtable = &tabview_vtable;
    tv->base.x = x;
    tv->base.y = y;
    tv->base.width = width;
    tv->base.height = height;
    tv->base.visible = true;
    tv->base.enabled = true;
    tv->base.focusable = true;
    tv->base.children = NULL;
    tv->base.child_count = 0;
    tv->base.user_data = NULL;

    tv->tab_titles = NULL;
    tv->tab_count = 0;
    tv->tab_capacity = 0;
    tv->selected_index = 0;
    tv->left_width = left_width;
    tv->focus_on_tabs = true;

    return UI2_WIDGET(tv);
}

int ui2_tabview_add_tab(ui2_tabview_t *tv, const char *title, ui2_layout_type_t layout_type) {
    if (!tv || !title) return -1;

    if (tv->tab_count >= tv->tab_capacity) {
        int new_cap = tv->tab_capacity ? tv->tab_capacity * 2 : 8;
        char **new_titles = (char **)realloc(tv->tab_titles, new_cap * sizeof(char *));
        if (!new_titles) return -1;
        tv->tab_titles = new_titles;

        ui2_widget_t **new_children = (ui2_widget_t **)realloc(
            tv->base.children, new_cap * sizeof(ui2_widget_t *));
        if (!new_children) return -1;
        tv->base.children = new_children;
        tv->tab_capacity = new_cap;
    }

    int idx = tv->tab_count;

    tv->tab_titles[idx] = (char *)malloc(strlen(title) + 1);
    if (!tv->tab_titles[idx]) return -1;
    strcpy(tv->tab_titles[idx], title);

    int cx = tv->base.x + tv->left_width + 1;
    int cw = tv->base.x + tv->base.width - cx;
    ui2_layout_t *layout = ui2_layout_create(cx, tv->base.y, cw, tv->base.height, layout_type);
    if (!layout) {
        free(tv->tab_titles[idx]);
        return -1;
    }
    layout->base.visible = (idx == 0);

    tv->base.children[idx] = UI2_WIDGET(layout);
    tv->tab_count++;
    tv->base.child_count = tv->tab_count;

    if (idx == 0) {
        tv->selected_index = 0;
        ui2_tabview_position_content(tv);
    }

    return idx;
}

void ui2_tabview_remove_tab(ui2_tabview_t *tv, int index) {
    if (!tv || index < 0 || index >= tv->tab_count) return;

    if (tv->base.children[index] && tv->base.children[index]->vtable &&
        tv->base.children[index]->vtable->destroy)
        tv->base.children[index]->vtable->destroy(tv->base.children[index]);

    free(tv->tab_titles[index]);

    for (int i = index; i < tv->tab_count - 1; i++) {
        tv->tab_titles[i] = tv->tab_titles[i + 1];
        tv->base.children[i] = tv->base.children[i + 1];
    }

    tv->tab_count--;
    tv->base.child_count = tv->tab_count;

    if (tv->selected_index >= tv->tab_count)
        tv->selected_index = tv->tab_count - 1;
    if (tv->selected_index < 0)
        tv->selected_index = 0;

    ui2_tabview_position_content(tv);
}

ui2_layout_t *ui2_tabview_get_content(ui2_tabview_t *tv, int tab_index) {
    if (!tv || tab_index < 0 || tab_index >= tv->tab_count) return NULL;
    return (ui2_layout_t *)tv->base.children[tab_index];
}

void ui2_tabview_set_selected(ui2_tabview_t *tv, int index) {
    if (!tv || tv->tab_count <= 0) return;
    if (index < 0) index = 0;
    if (index >= tv->tab_count) index = tv->tab_count - 1;
    tv->selected_index = index;
    ui2_tabview_position_content(tv);
}

int ui2_tabview_get_selected(const ui2_tabview_t *tv) {
    return tv ? tv->selected_index : -1;
}
