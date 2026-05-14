#ifndef READER_STATE_H
#define READER_STATE_H

#include "reader_page.h"
#include "reader_md.h"
#include "ui.h"
#include <stdio.h>

#define MARGIN 2
#define MAX_FILES 64
#define MAX_PATH 256
#define MAX_TOC_ENTRIES 64

#define KEY_LAST_FILE "reader_last_file"
#define KEY_LEGACY_LAST_FILE "reader_file"
#define KEY_BOOK_OFFSET_PREFIX "reader_off"
#define KEY_BOOK_PAGE_PREFIX "reader_page"

typedef enum {
    MODE_FILE_LIST,
    MODE_READING,
    MODE_GOTO,
    MODE_TOC,
} reader_mode_t;

typedef struct {
    char title[64];
    uint8_t level;
    int page_number;
    uint32_t file_offset;
} toc_entry_t;

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
    int total_pages;

    // Goto state
    char goto_buf[8];
    ui_text_input_widget_t goto_widget;

    // TOC state
    toc_entry_t toc[MAX_TOC_ENTRIES];
    int toc_count;
    int toc_selected;

    // File list touch button positions
    int btn_up_x;
    int btn_open_x;
    int btn_down_x;
    int btn_exit_x;
    int btn_row;
    int btn_w;
} reader_state_t;

#endif
