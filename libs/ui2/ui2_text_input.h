#ifndef UI2_TEXT_INPUT_H
#define UI2_TEXT_INPUT_H

#include "ui2_widget.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui2_text_input_cb)(void *user_data);

typedef struct {
    ui2_widget_t base;
    char *buffer;
    int max_len;
    char *title;
    char *label;
    char *hint_left;
    char *hint_right;
    bool mask_input;
    uint8_t title_fg;
    uint8_t title_bg;
    uint8_t label_fg;
    uint8_t label_bg;
    uint8_t text_fg;
    uint8_t text_bg;
    uint8_t hint_fg;
    uint8_t hint_bg;
    ui2_text_input_cb on_confirm;
    ui2_text_input_cb on_cancel;
    void *cb_data;
} ui2_text_input_t;

ui2_text_input_t *ui2_text_input_create(int x, int y, int width, int height);
void ui2_text_input_destroy(ui2_widget_t *widget);

void ui2_text_input_set_buffer(ui2_text_input_t *input, char *buffer, int max_len);
void ui2_text_input_set_title(ui2_text_input_t *input, const char *title);
void ui2_text_input_set_label(ui2_text_input_t *input, const char *label);
void ui2_text_input_set_hints(ui2_text_input_t *input, const char *left, const char *right);
void ui2_text_input_set_mask(ui2_text_input_t *input, bool mask);
void ui2_text_input_set_colors(ui2_text_input_t *input,
                                uint8_t title_fg, uint8_t title_bg,
                                uint8_t label_fg, uint8_t label_bg,
                                uint8_t text_fg, uint8_t text_bg,
                                uint8_t hint_fg, uint8_t hint_bg);
void ui2_text_input_set_callbacks(ui2_text_input_t *input,
                                   ui2_text_input_cb on_confirm,
                                   ui2_text_input_cb on_cancel,
                                   void *user_data);
void ui2_text_input_clear(ui2_text_input_t *input);

#ifdef __cplusplus
}
#endif

#endif
