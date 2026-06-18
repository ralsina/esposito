#include "ui2_toolbar.h"
#include <stdlib.h>

static int label_display_width(const char *label) {
    if (!label) return 0;
    int cols = 0;
    for (int i = 0; label[i]; ) {
        unsigned char b = (unsigned char)label[i];
        if ((b & 0xE0) == 0xC0) i += 2;
        else if ((b & 0xF0) == 0xE0) i += 3;
        else if ((b & 0xF8) == 0xF0) i += 4;
        else i += 1;
        cols++;
    }
    return cols;
}

ui2_layout_t *ui2_toolbar_create(int x, int y, int width, int height,
                                 const ui2_toolbar_item_t *items, int count) {
    if (count <= 0 || !items) return NULL;

    if (count > width) count = width;

    int gap = 1;
    int btn_width = (width - (count - 1) * gap) / count;
    if (btn_width < 1) {
        gap = 0;
        btn_width = width / count;
        if (btn_width < 1) btn_width = 1;
    }

    int first_dw = label_display_width(items[0].label);
    bool all_same = true;
    for (int i = 1; i < count; i++) {
        if (label_display_width(items[i].label) != first_dw) {
            all_same = false;
            break;
        }
    }

    if (all_same && first_dw > 0) {
        while (btn_width > first_dw && (btn_width - first_dw) % 2 != 0) {
            btn_width--;
        }
    }

    int total_used = count * btn_width + (count - 1) * gap;
    int leftover = width - total_used;

    ui2_layout_t *layout = ui2_layout_create(x + leftover / 2, y,
                                             total_used, height,
                                             UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(layout, gap);

    for (int i = 0; i < count; i++) {
        ui2_button_t *btn = ui2_button_create(0, 0, btn_width, height, items[i].label);
        if (items[i].on_click) {
            ui2_button_set_callback(btn, items[i].on_click, items[i].user_data);
        }
        ui2_layout_add(layout, UI2_WIDGET(btn));
    }

    return layout;
}
