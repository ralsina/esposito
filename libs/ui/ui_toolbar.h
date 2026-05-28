#ifndef UI_TOOLBAR_H
#define UI_TOOLBAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "os_core.h"
#include "text_mode.h"
#include "ui_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_toolbar {
    int y;
    int height;
    int button_count;
    ui_button_t **buttons;
    int *button_widths;
    bool visible;
} ui_toolbar_t;

ui_toolbar_t* ui_toolbar_create(int y, int height, int button_count, const char **labels);
void ui_toolbar_destroy(ui_toolbar_t *toolbar);
void ui_toolbar_draw(ui_toolbar_t *toolbar);
bool ui_toolbar_handle_touch(ui_toolbar_t *toolbar, const event_t *event);
ui_button_t* ui_toolbar_get_button(ui_toolbar_t *toolbar, int index);
void ui_toolbar_set_visible(ui_toolbar_t *toolbar, bool visible);

#ifdef __cplusplus
}
#endif

#endif
