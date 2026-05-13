#ifndef READER_STARTUP_H
#define READER_STARTUP_H

#include "reader_state.h"

void reader_startup_init(reader_state_t *state, int *bold_pending, int *underline_pending);

#endif
