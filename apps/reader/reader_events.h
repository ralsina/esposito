#ifndef READER_EVENTS_H
#define READER_EVENTS_H

#include "os_core.h"
#include "reader_state.h"

int reader_events_open_book(reader_state_t *state, const char *path, int *bold_pending, int *underline_pending);
void reader_events_enter_reading_mode(reader_state_t *state, int *bold_pending, int *underline_pending);
void reader_events_show_file_list(reader_state_t *state);
void reader_events_handle_event(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void));

#endif
