#ifndef READER_EVENTS_H
#define READER_EVENTS_H

#include "os_core.h"
#include "reader_state.h"

int reader_events_open_book(reader_state_t *state, const char *path, int *bold_pending, int *underline_pending);
void reader_events_enter_reading_mode(reader_state_t *state, int *bold_pending, int *underline_pending);
void reader_events_show_file_list(reader_state_t *state);
void reader_events_handle_event(reader_state_t *state, const event_t *event, int *bold_pending, int *underline_pending, void (*launch_app_list)(void));

// Button widget callbacks
void on_file_list_up_click(ui_button_t *button, void *user_data);
void on_file_list_open_click(ui_button_t *button, void *user_data);
void on_file_list_down_click(ui_button_t *button, void *user_data);
void on_file_list_exit_click(ui_button_t *button, void *user_data);
void on_toc_up_click(ui_button_t *button, void *user_data);
void on_toc_jump_click(ui_button_t *button, void *user_data);
void on_toc_down_click(ui_button_t *button, void *user_data);
void on_toc_back_click(ui_button_t *button, void *user_data);
void on_reading_toc_click(ui_button_t *button, void *user_data);
void on_reading_back_click(ui_button_t *button, void *user_data);

#endif
