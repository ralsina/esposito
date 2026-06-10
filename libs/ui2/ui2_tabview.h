#ifndef UI2_TABVIEW_H
#define UI2_TABVIEW_H

#include "ui2_widget.h"
#include "ui2_layout.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI2_TAB_MAX_TITLE 32

typedef struct ui2_tabview_s {
    ui2_widget_t base;
    char **tab_titles;
    int tab_count;
    int tab_capacity;
    int selected_index;
    int left_width;
    bool focus_on_tabs;
} ui2_tabview_t;

ui2_widget_t *ui2_tabview_create(int x, int y, int width, int height, int left_width);
int ui2_tabview_add_tab(ui2_tabview_t *tv, const char *title, ui2_layout_type_t layout_type);
void ui2_tabview_remove_tab(ui2_tabview_t *tv, int index);
ui2_layout_t *ui2_tabview_get_content(ui2_tabview_t *tv, int tab_index);
void ui2_tabview_set_selected(ui2_tabview_t *tv, int index);
int ui2_tabview_get_selected(const ui2_tabview_t *tv);

#ifdef __cplusplus
}
#endif

#endif
