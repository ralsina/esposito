#ifndef UI2_LIST_H
#define UI2_LIST_H

#include "ui2_widget.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui2_list_selection_cb)(int new_selection, void *user_data);
typedef void (*ui2_list_activate_cb)(int item_index, void *user_data);

typedef struct {
    ui2_widget_t base;
    const char **items;
    int count;
    int selected;
    int scroll_offset;
    int visible_rows;
    char *title;
    uint8_t normal_fg;
    uint8_t normal_bg;
    uint8_t selected_fg;
    uint8_t selected_bg;
    uint8_t border_fg;
    ui2_list_selection_cb on_selection_changed;
    ui2_list_activate_cb on_item_activated;
    void *cb_data;
} ui2_list_t;

ui2_list_t *ui2_list_create(int x, int y, int width, int height);
void ui2_list_destroy(ui2_widget_t *widget);

void ui2_list_set_items(ui2_list_t *list, const char **items, int count);
void ui2_list_set_title(ui2_list_t *list, const char *title);
void ui2_list_set_selection(ui2_list_t *list, int index);
int ui2_list_get_selection(const ui2_list_t *list);
void ui2_list_set_callbacks(ui2_list_t *list,
                             ui2_list_selection_cb on_changed,
                             ui2_list_activate_cb on_activated,
                             void *user_data);
void ui2_list_set_colors(ui2_list_t *list, uint8_t normal_fg, uint8_t normal_bg,
                          uint8_t selected_fg, uint8_t selected_bg, uint8_t border_fg);

#ifdef __cplusplus
}
#endif

#endif
