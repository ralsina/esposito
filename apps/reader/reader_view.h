#ifndef READER_VIEW_H
#define READER_VIEW_H

#include "reader_state.h"

void reader_view_draw_reading_page(const reader_state_t *state, int *bold_pending, int *underline_pending);
void reader_view_draw_toc(reader_state_t *state);
void reader_view_update_toc_selection(const reader_state_t *state, int previous_selected);
void reader_view_draw_file_list(reader_state_t *state);
void reader_view_clear_widgets(reader_state_t *state);

#endif
