#ifndef UI2_TOOLBAR_H
#define UI2_TOOLBAR_H

#include "ui2_layout.h"
#include "ui2_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;
    ui2_button_cb on_click;
    void *user_data;
} ui2_toolbar_item_t;

ui2_layout_t *ui2_toolbar_create(int x, int y, int width, int height,
                                 const ui2_toolbar_item_t *items, int count);

#ifdef __cplusplus
}
#endif

#endif
