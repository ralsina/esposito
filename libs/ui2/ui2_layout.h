#ifndef UI2_LAYOUT_H
#define UI2_LAYOUT_H

#include "ui2_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI2_LAYOUT_ABSOLUTE,
    UI2_LAYOUT_VERTICAL,
    UI2_LAYOUT_HORIZONTAL,
} ui2_layout_type_t;

typedef struct {
    ui2_widget_t base;
    ui2_layout_type_t type;
    int gap;
    int capacity;
} ui2_layout_t;

ui2_layout_t *ui2_layout_create(int x, int y, int width, int height, ui2_layout_type_t type);
void ui2_layout_destroy(ui2_widget_t *widget);

void ui2_layout_add(ui2_layout_t *layout, ui2_widget_t *child);
void ui2_layout_remove(ui2_layout_t *layout, ui2_widget_t *child);
void ui2_layout_set_gap(ui2_layout_t *layout, int gap);
void ui2_layout_clear(ui2_layout_t *layout);

#ifdef __cplusplus
}
#endif

#endif
