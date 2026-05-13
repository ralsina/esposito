#ifndef READER_CORE_H
#define READER_CORE_H

#include "reader_state.h"
#include <stddef.h>

void reader_build_book_key(char *out, size_t out_size, const char *prefix, const char *path);
void reader_save_current_book_progress(reader_state_t *state);
void reader_scan_md_files(reader_state_t *state);
int reader_find_file_index_by_path(const reader_state_t *state, const char *path);
void reader_close_current_file(reader_state_t *state);
int reader_open_file(reader_state_t *state, const char *path);
int reader_load_current_page(reader_state_t *state, int *bold_pending, int *underline_pending);

#endif
