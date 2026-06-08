#ifndef UI2_WIDGET_H
#define UI2_WIDGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui2_widget_s ui2_widget_t;

typedef struct {
    void (*draw)(ui2_widget_t *widget);
    bool (*handle_key)(ui2_widget_t *widget, char key);
    bool (*handle_touch)(ui2_widget_t *widget, int col, int row, bool pressed);
    void (*on_focus)(ui2_widget_t *widget, bool focused);
    void (*destroy)(ui2_widget_t *widget);
} ui2_widget_vtable_t;

struct ui2_widget_s {
    const ui2_widget_vtable_t *vtable;
    int x, y, width, height;
    bool visible;
    bool enabled;
    bool focusable;
    ui2_widget_t **children;
    int child_count;
    void *user_data;
};

#define UI2_WIDGET(w) (&(w)->base)

bool ui2_widget_default_handle_key(ui2_widget_t *widget, char key);
bool ui2_widget_default_handle_touch(ui2_widget_t *widget, int col, int row, bool pressed);
void ui2_widget_default_on_focus(ui2_widget_t *widget, bool focused);

#ifdef __cplusplus
}
#endif

#endif
