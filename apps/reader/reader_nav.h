#ifndef READER_NAV_H
#define READER_NAV_H

#include "reader_state.h"
#include "ui2_text_input.h"

int reader_compute_page_number(reader_state_t *state);
void reader_nav_next_page(reader_state_t *state, int *bold_pending, int *underline_pending);
void reader_nav_prev_page(reader_state_t *state, int *bold_pending, int *underline_pending);
void reader_nav_start_goto(reader_state_t *state);
void reader_nav_handle_goto_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending);
void reader_nav_start_search(reader_state_t *state);
void reader_nav_handle_search_key(reader_state_t *state, char key, int *bold_pending, int *underline_pending);

// OSK callback functions (made public for event handling)
void on_goto_confirm(void *user_data);
void on_goto_cancel(void *user_data);
void on_search_confirm(void *user_data);
void on_search_cancel(void *user_data);

#endif
