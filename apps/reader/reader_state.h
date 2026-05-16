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
    MODE_SEARCH,
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
    char (*file_names)[64];
    char (*file_paths)[MAX_PATH];
    const char **file_ptrs;
    int file_count;
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
    ui_text_input_widget_t *goto_widget;

    // Search state
    char search_buf[64];
    ui_text_input_widget_t *search_widget;
    char search_status[80];

    // TOC state
    toc_entry_t *toc;
    int toc_count;
    int toc_selected;
    const char **toc_titles;  // Persistent array of title pointers for list widget

    // Button widgets (managed by UI library)
    ui_button_t *btn_up;
    ui_button_t *btn_open;
    ui_button_t *btn_down;
    ui_button_t *btn_exit;
    ui_button_t *btn_jump;
    ui_button_t *btn_back;

    // List widgets
    ui_list_widget_t *toc_list;
    ui_list_widget_t *file_list;

    // Button coordinates for file list (temporary, until file list uses widgets)
    int btn_up_x;
    int btn_open_x;
    int btn_down_x;
    int btn_exit_x;
    int btn_row;
    int btn_w;

    // Function pointer to launch app list (for exit button)
    void (*launch_app_list)(void);
} reader_state_t;

#endif
