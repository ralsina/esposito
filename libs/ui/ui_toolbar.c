#include "ui_toolbar.h"
#include "os_core.h"
#include "hardware.h"
#include <stdlib.h>
#include <string.h>

static int label_display_width(const char *s) {
    if (!s) return 0;
    int cols = 0;
    for (int i = 0; s[i]; ) {
        unsigned char b = (unsigned char)s[i];
        if ((b & 0xE0) == 0xC0) i += 2;
        else if ((b & 0xF0) == 0xE0) i += 3;
        else if ((b & 0xF8) == 0xF0) i += 4;
        else i += 1;
        cols++;
    }
    return cols;
}

static int match_parity(int width, int parity) {
    if (parity < 0) return width;
    if ((width % 2) != (parity % 2)) width++;
    return width;
}

ui_toolbar_t* ui_toolbar_create(int y, int height, int button_count, const char **labels) {
    if (button_count < 1) {
        return NULL;
    }

    ui_toolbar_t *toolbar = (ui_toolbar_t*)malloc(sizeof(ui_toolbar_t));
    if (!toolbar) {
        return NULL;
    }

    toolbar->y = y;
    toolbar->height = height;
    toolbar->button_count = button_count;
    toolbar->visible = true;

    toolbar->buttons = (ui_button_t**)malloc(sizeof(ui_button_t*) * button_count);
    toolbar->button_widths = (int*)malloc(sizeof(int) * button_count);

    if (!toolbar->buttons || !toolbar->button_widths) {
        free(toolbar->buttons);
        free(toolbar->button_widths);
        free(toolbar);
        return NULL;
    }

    int cols = text_mode_get_cols();
    int gap = 1;
    int total_gaps = (button_count - 1) * gap;
    int available = cols - total_gaps;
    int btn_width = available / button_count;
    int remainder = available - (btn_width * button_count);

    int total_group_width = 0;
    for (int i = 0; i < button_count; i++) {
        int label_w = label_display_width(labels ? labels[i] : NULL);
        int w = btn_width + (i < remainder ? 1 : 0);
        if (w < 3) w = 3;
        w = match_parity(w, label_w);
        toolbar->button_widths[i] = w;
        total_group_width += w;
    }
    total_group_width += total_gaps;

    int start_x = (cols - total_group_width) / 2;
    if (start_x < 0) start_x = 0;

    int x = start_x;
    for (int i = 0; i < button_count; i++) {
        toolbar->buttons[i] = ui_button_create(x, y, toolbar->button_widths[i], height, labels ? labels[i] : NULL);
        x += toolbar->button_widths[i] + gap;
    }

    return toolbar;
}

void ui_toolbar_destroy(ui_toolbar_t *toolbar) {
    if (!toolbar) {
        return;
    }

    for (int i = 0; i < toolbar->button_count; i++) {
        if (toolbar->buttons[i]) {
            ui_button_destroy(toolbar->buttons[i]);
            toolbar->buttons[i] = NULL;
        }
    }

    free(toolbar->buttons);
    free(toolbar->button_widths);
    free(toolbar);
}

void ui_toolbar_draw(ui_toolbar_t *toolbar) {
    if (!toolbar || !toolbar->visible) {
        return;
    }

    for (int i = 0; i < toolbar->button_count; i++) {
        ui_button_draw(toolbar->buttons[i]);
    }
}

bool ui_toolbar_handle_touch(ui_toolbar_t *toolbar, const event_t *event) {
    if (!toolbar || !toolbar->visible) {
        return false;
    }

    for (int i = 0; i < toolbar->button_count; i++) {
        if (ui_button_handle_touch(toolbar->buttons[i], event)) {
            return true;
        }
    }

    return false;
}

ui_button_t* ui_toolbar_get_button(ui_toolbar_t *toolbar, int index) {
    if (!toolbar || index < 0 || index >= toolbar->button_count) {
        return NULL;
    }

    return toolbar->buttons[index];
}

void ui_toolbar_set_visible(ui_toolbar_t *toolbar, bool visible) {
    if (!toolbar) {
        return;
    }

    toolbar->visible = visible;
}
