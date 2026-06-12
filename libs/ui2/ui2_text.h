#ifndef UI2_TEXT_H
#define UI2_TEXT_H

#include "ui2_widget.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ui2_widget_t base;
    bool draw_border;
    uint8_t border_fg;
    char *content;
    uint8_t content_fg;
    uint8_t content_attr;
} ui2_text_t;

ui2_text_t *ui2_text_create(int x, int y, int width, int height);
void ui2_text_destroy(ui2_widget_t *widget);

void ui2_text_set_border(ui2_text_t *text, bool draw, uint8_t border_fg);
void ui2_text_set_content(ui2_text_t *text, const char *str, uint8_t fg, uint8_t attr);
void ui2_text_clear_content(ui2_text_t *text);

void ui2_text_print_at(ui2_text_t *text, int rel_x, int rel_y,
                        const char *str, uint8_t fg, uint8_t bg, uint8_t attr);
void ui2_text_printf_at(ui2_text_t *text, int rel_x, int rel_y,
                         uint8_t fg, uint8_t bg, uint8_t attr,
                         const char *fmt, ...);

void ui2_text_clear(ui2_text_t *text);

int ui2_text_get_content_width(const ui2_text_t *text);
int ui2_text_get_content_height(const ui2_text_t *text);

#ifdef __cplusplus
}
#endif

#endif
