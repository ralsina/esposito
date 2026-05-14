#ifndef PAINT_STORAGE_H
#define PAINT_STORAGE_H

#include <stdbool.h>
#include "paint_state.h"

bool paint_storage_save(const paint_state_t *state, const char *path);
bool paint_storage_load(paint_state_t *state, const char *path);

#endif
