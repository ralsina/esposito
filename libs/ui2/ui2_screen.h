#ifndef UI2_SCREEN_H
#define UI2_SCREEN_H

#include "os_core.h"
#include "ui2_widget.h"
#include "ui2_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui2_screen_s ui2_screen_t;

ui2_screen_t *ui2_screen_create(void);
void ui2_screen_destroy(ui2_screen_t *screen);

void ui2_screen_set_root(ui2_screen_t *screen, ui2_layout_t *root);

void ui2_screen_shortcut_add(ui2_screen_t *screen, char key, uint8_t modifiers, ui2_widget_t *target);
void ui2_screen_shortcut_remove(ui2_screen_t *screen, char key, uint8_t modifiers);

void ui2_screen_set_focus_keys(ui2_screen_t *screen,
                                char next_key, uint8_t next_mods,
                                char prev_key, uint8_t prev_mods);
void ui2_screen_focus_set(ui2_screen_t *screen, ui2_widget_t *widget);
void ui2_screen_focus_next(ui2_screen_t *screen);
void ui2_screen_focus_prev(ui2_screen_t *screen);
ui2_widget_t *ui2_screen_focus_get(ui2_screen_t *screen);

bool ui2_screen_handle_event(ui2_screen_t *screen, event_t *event);
void ui2_screen_render(ui2_screen_t *screen);

#ifdef __cplusplus
}
#endif

#endif
