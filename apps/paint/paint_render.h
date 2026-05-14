#ifndef PAINT_RENDER_H
#define PAINT_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include "paint_state.h"

uint16_t paint_palette_rgb565(uint8_t index);
void paint_render_all(const paint_state_t *state);
void paint_render_pixel(const paint_state_t *state, int x, int y);
void paint_render_preview_line(const paint_state_t *state);
void paint_render_ui(const paint_state_t *state);
void paint_render_status(paint_state_t *state, const char *message);

#endif
