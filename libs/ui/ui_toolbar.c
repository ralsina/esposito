#include "ui_toolbar.h"
#include "os_core.h"
#include "hardware.h"
#include <stdlib.h>

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

    // Count the total display width of all labels
    int total_labels = 0;
    for (int i = 0; i < button_count; i++) {
        total_labels += label_display_width(labels ? labels[i] : NULL);
    }

    int slack = cols - total_labels;
    int num_gaps = button_count - 1;

    if (slack < 0) {
        // Doesn't fit — each button is exactly its label width, left-aligned
        int x = 0;
        for (int i = 0; i < button_count; i++) {
            int w = label_display_width(labels ? labels[i] : NULL);
            toolbar->button_widths[i] = w;
            toolbar->buttons[i] = ui_button_create(x, y, w, height, labels ? labels[i] : NULL);
            x += w;
        }
        return toolbar;
    }

// New algorithm: prioritize 3-cell wide buttons, no gaps if needed
    int gap = 0;        // No gaps - prioritize button width
    int margin = 0;      // No margins - prioritize button width
    int per_button_extra = 0; // Extra width per button for expansion
    
    // First priority: ALWAYS make buttons at least 3 cells wide if possible
    int min_button_width = 3;
    int min_total_width = button_count * min_button_width;
    
    if (slack >= min_total_width) {
        // We can make all buttons 3 cells wide - PRIORITY 1
        per_button_extra = min_button_width - 1; // 1 (label) + extra = 3 minimum
        gap = 0; // NO gaps - prioritize width over gaps
    } else {
        // Can't reach 3-cell width - use ALL available space for buttons
        per_button_extra = slack / button_count;
        gap = 0; // NO gaps even for smaller buttons - maximize width
    }

    // Build buttons with extra width
    int total_width = 0;
    int start_x = 0; // Start from left, no margins
    
    for (int i = 0; i < button_count; i++) {
        int label_w = label_display_width(labels ? labels[i] : NULL);
        int button_width = label_w + per_button_extra;
        toolbar->button_widths[i] = button_width;
        total_width += button_width;
    }
    total_width += num_gaps * gap;

    // Center the toolbar if there's extra space
    start_x = (cols - total_width) / 2;

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
