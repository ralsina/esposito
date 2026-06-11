#ifndef UI2_BUFFER_H
#define UI2_BUFFER_H

#include "ui2_widget.h"
#include "text_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *text;
    uint8_t fg;
    uint8_t bg;
    uint8_t attrs;
} ui2_buffer_line_t;

typedef struct {
    ui2_widget_t base;
    ui2_buffer_line_t *lines;
    int max_lines;
    int count;
    int head;
    int scroll_offset;
    int visible_rows;
    bool scroll_to_bottom;
} ui2_buffer_t;

ui2_buffer_t *ui2_buffer_create(int x, int y, int width, int height, int max_lines);
void ui2_buffer_destroy(ui2_widget_t *widget);

int ui2_buffer_add_line(ui2_buffer_t *buf, const char *text, uint8_t fg, uint8_t bg, uint8_t attrs);
void ui2_buffer_clear(ui2_buffer_t *buf);
void ui2_buffer_scroll(ui2_buffer_t *buf, int delta);
void ui2_buffer_set_scroll_to_bottom(ui2_buffer_t *buf, bool auto_scroll);
int ui2_buffer_get_count(const ui2_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif
