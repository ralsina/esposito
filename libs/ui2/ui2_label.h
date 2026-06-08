#ifndef UI2_LABEL_H
#define UI2_LABEL_H

#include "ui2_widget.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ui2_widget_t base;
    char *text;
    uint8_t color;
    uint8_t attr;
} ui2_label_t;

ui2_label_t *ui2_label_create(int x, int y, const char *text, uint8_t color, uint8_t attr);
void ui2_label_destroy(ui2_widget_t *widget);

void ui2_label_set_text(ui2_label_t *label, const char *text);
void ui2_label_set_color(ui2_label_t *label, uint8_t color, uint8_t attr);

#ifdef __cplusplus
}
#endif

#endif
