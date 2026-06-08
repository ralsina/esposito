#ifndef UI2_PROGRESSBAR_H
#define UI2_PROGRESSBAR_H

#include "ui2_widget.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ui2_widget_t base;
    int value;
    int max_value;
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t border_color;
    bool show_percent;
} ui2_progressbar_t;

ui2_progressbar_t *ui2_progressbar_create(int x, int y, int width);
void ui2_progressbar_destroy(ui2_widget_t *widget);

void ui2_progressbar_set_value(ui2_progressbar_t *bar, int value, int max_value);
void ui2_progressbar_set_colors(ui2_progressbar_t *bar, uint8_t fg, uint8_t bg, uint8_t border);

#ifdef __cplusplus
}
#endif

#endif
