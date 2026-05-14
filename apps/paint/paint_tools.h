#ifndef PAINT_TOOLS_H
#define PAINT_TOOLS_H

#include "paint_state.h"

void paint_tools_handle_touch(paint_state_t *state, int x, int y, bool pressed, void (*launch_app_list)(void));

#endif
