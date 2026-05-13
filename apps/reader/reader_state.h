#ifndef READER_STATE_H
#define READER_STATE_H

#include "reader_page.h"
#include "reader_md.h"
#include <stdio.h>

#define MARGIN 2
#define MAX_FILES 64
#define MAX_PATH 256

#define KEY_LAST_FILE "reader_last_file"
#define KEY_LEGACY_LAST_FILE "reader_file"
#define KEY_BOOK_OFFSET_PREFIX "reader_off"
#define KEY_BOOK_PAGE_PREFIX "reader_page"

typedef enum {
    MODE_FILE_LIST,
    MODE_READING,
    MODE_GOTO,
} reader_mode_t;

typedef struct {
    reader_mode_t mode;

    // File list state
    int file_count;
    char file_names[MAX_FILES][64];
    char file_paths[MAX_FILES][MAX_PATH];
    const char *file_ptrs[MAX_FILES];
    int file_selected;

    // Reading state
    char current_file[MAX_PATH];
    FILE *file;
    page_cache_t page_cache;
    rendered_line_t lines[MAX_RENDERED_LINES];
    int line_count;
    int screen_width;
    int content_rows;
    int page_number;

    // Goto state
    char goto_buf[8];
    int goto_pos;

    // File list touch button positions
    int btn_up_x;
    int btn_open_x;
    int btn_down_x;
    int btn_exit_x;
    int btn_row;
    int btn_w;
} reader_state_t;

#endif
