#ifndef READER_STATE_H
#define READER_STATE_H

#include "reader_renderer.h"
#include "reader_render_pipeline.h"
#include "ui2.h"
#include <stdio.h>

#define MARGIN 2
#define MAX_FILES 64
#define MAX_PATH 256
#define MAX_TOC_ENTRIES 64

#define KEY_LAST_FILE "reader_last_file"
#define KEY_LEGACY_LAST_FILE "reader_file"
#define KEY_BOOK_OFFSET_PREFIX "reader_off"

typedef enum {
    MODE_FILE_LIST,
    MODE_READING,
    MODE_GOTO,
    MODE_SEARCH,
    MODE_TOC,
    MODE_RECEIVING,
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
    char (*file_names)[64];
    char (*file_paths)[MAX_PATH];
    const char **file_ptrs;
    int file_count;
    int file_selected;

    // Reading state
    char current_file[MAX_PATH];
    FILE *file;
    page_cache_t page_cache;
    rendered_line_t *lines;
    int line_count;
    int line_buf_size;
    int screen_width;
    int content_rows;
    reader_render_pipeline_t pipeline;
    int page_number;
    int total_pages;
    int64_t last_save_us;

    // Goto state
    char goto_buf[8];
    ui2_text_input_t *goto_widget;

    // Search state
    char search_buf[64];
    ui2_text_input_t *search_widget;
    char search_status[80];

    // TOC state
    toc_entry_t *toc;
    int toc_count;
    int toc_selected;
    const char **toc_titles;

    // UI screen
    ui2_screen_t *screen;

    // Reading mode buttons (owned by screen layout, pointers kept for custom render)
    ui2_button_t *btn_jump;
    ui2_button_t *btn_find;
    ui2_button_t *btn_goto;
    ui2_button_t *btn_back;

    // Receiving mode cancel button
    ui2_button_t *btn_cancel;

    // Function pointer to launch app list (for exit button)
    void (*launch_app_list)(void);

    // Ignore this many keyboard/touch events after download (drains phantom events)
    int ignore_events;

    // Filename being received via serial
    char receiving_filename[64];
} reader_state_t;

#endif
