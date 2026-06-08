#ifndef UI2_BUTTON_H
#define UI2_BUTTON_H

#include "ui2_widget.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui2_button_s ui2_button_t;

typedef void (*ui2_button_cb)(ui2_button_t *button, void *user_data);

struct ui2_button_s {
    ui2_widget_t base;
    char *text;
    uint16_t fg_color;
    uint16_t bg_color;
    ui2_button_cb on_click;
    void *click_data;
};

ui2_button_t *ui2_button_create(int x, int y, int width, int height, const char *text);
void ui2_button_destroy(ui2_widget_t *widget);

void ui2_button_set_text(ui2_button_t *btn, const char *text);
void ui2_button_set_callback(ui2_button_t *btn, ui2_button_cb cb, void *data);
void ui2_button_set_colors(ui2_button_t *btn, uint16_t fg, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif
