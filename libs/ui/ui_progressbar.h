#ifndef UI_PROGRESSBAR_H
#define UI_PROGRESSBAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "os_core.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_progressbar {
    int x, y;
    int width;
    int value;
    int max_value;
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t border_color;
    bool show_percent;
    bool visible;
} ui_progressbar_t;

ui_progressbar_t* ui_progressbar_create(int x, int y, int width);
void ui_progressbar_destroy(ui_progressbar_t *bar);
void ui_progressbar_draw(const ui_progressbar_t *bar);
void ui_progressbar_set_value(ui_progressbar_t *bar, int value, int max_value);
void ui_progressbar_set_colors(ui_progressbar_t *bar, uint8_t fg, uint8_t bg, uint8_t border);
void ui_progressbar_set_visible(ui_progressbar_t *bar, bool visible);

#ifdef __cplusplus
}
#endif

#endif
