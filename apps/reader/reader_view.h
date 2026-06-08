#ifndef READER_VIEW_H
#define READER_VIEW_H

#include "reader_state.h"

void reader_view_setup_file_list(reader_state_t *state);
void reader_view_setup_reading(reader_state_t *state);
void reader_view_setup_toc(reader_state_t *state);
void reader_view_setup_goto(reader_state_t *state);
void reader_view_setup_search(reader_state_t *state);
void reader_view_setup_receiving(reader_state_t *state);

void reader_view_render_reading(reader_state_t *state, int *bold_pending, int *underline_pending);
void reader_view_render_receiving(reader_state_t *state);

void reader_view_update_progress(const reader_state_t *state, size_t received, size_t total);

#endif
