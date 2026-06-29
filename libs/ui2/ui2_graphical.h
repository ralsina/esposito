#ifndef UI2_GRAPHICAL_H
#define UI2_GRAPHICAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui2_set_graphical(bool graphical);
bool ui2_is_graphical(void);

int ui2_graphical_px(int cells);
int ui2_graphical_py(int cells);
int ui2_graphical_pw(int cells);
int ui2_graphical_ph(int cells);

uint16_t ui2_graphical_color(uint8_t palette_index);

void ui2_draw_rounded_rect(int px, int py, int pw, int ph, int r,
                           uint16_t fill, uint16_t border);

void ui2_fill_rounded_rect(int px, int py, int pw, int ph, int r,
                           uint16_t fill);

uint16_t ui2_lighten_color(uint16_t color);
uint16_t ui2_darken_color(uint16_t color);

#ifdef __cplusplus
}
#endif

#endif
