#include "reader_view.h"

#include "reader_events.h"
#include "reader_md.h"
#include "text_mode.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_LIST_BTN_WIDTH 13
#define FILE_LIST_BTN_GAP 2
#define FILE_LIST_BTN_UP_LABEL "  UP  "
#define FILE_LIST_BTN_OPEN_LABEL " OPEN "
#define FILE_LIST_BTN_DOWN_LABEL " DOWN "
#define FILE_LIST_BTN_EXIT_LABEL " EXIT "
#define TOC_BTN_JUMP_LABEL " JUMP "
#define TOC_BTN_BACK_LABEL " BACK "

static int toc_scroll_for_selection(int selected, int list_rows) {
    if (selected >= list_rows) {
        return selected - list_rows + 1;
    }
    return 0;
}

static void draw_toc_row(const reader_state_t *state, int row_index, int toc_index, int selected) {
    int cols = text_mode_get_cols();
    int y = 2 + row_index;

    for (int x = 2; x < cols - 1; x++) {
        text_mode_print_at_color(x, y, " ", TEXT_COLOR_WHITE);
    }

    if (toc_index < 0 || toc_index >= state->toc_count) {
        return;
    }

    const toc_entry_t *entry = &state->toc[toc_index];
    char page_label[8];
    snprintf(page_label, sizeof(page_label), "p.%d", entry->page_number);
    int page_len = (int)strlen(page_label);

    int indent = (entry->level > 1) ? (entry->level - 1) * 2 : 0;
    if (indent > 8) {
        indent = 8;
    }

    int title_x = 4 + indent;
    int title_max = cols - title_x - 3 - page_len;
    if (title_max < 4) {
        title_max = 4;
    }

    char title[64];
    strncpy(title, entry->title, sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    if ((int)strlen(title) > title_max) {
        title[title_max - 1] = '.';
        title[title_max] = '\0';
    }

    uint8_t row_color = selected ? TEXT_COLOR_GREEN : TEXT_COLOR_WHITE;
    text_mode_print_at_color(2, y, selected ? ">" : " ", row_color);
    text_mode_print_at_color(title_x, y, title, row_color);
    text_mode_print_at_color(cols - 2 - page_len, y, page_label, TEXT_COLOR_CYAN);
}

static void draw_rich_line(int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t base_attr, int *bold_pending, int *underline_pending) {
    int cur_x = x;
    uint8_t attr = base_attr;
    int bold_active = 0;
    int underline_active = 0;

    if (*bold_pending) {
        attr = base_attr | TEXT_ATTR_BOLD;
        bold_active = 1;
        *bold_pending = 0;
    }
    if (*underline_pending) {
        attr |= TEXT_ATTR_UNDERLINE;
        underline_active = 1;
        *underline_pending = 0;
    }

    while (*text) {
        if (text[0] == '*' && text[1] == '*') {
            attr = (attr & TEXT_ATTR_BOLD) ? base_attr : (base_attr | TEXT_ATTR_BOLD);
            text += 2;
            bold_active = (attr & TEXT_ATTR_BOLD) ? 1 : 0;
            continue;
        }
        if (*text == MD_FORMAT_TOGGLE) {
            if (attr & TEXT_ATTR_UNDERLINE) {
                attr &= ~TEXT_ATTR_UNDERLINE;
                underline_active = 0;
            } else {
                attr |= TEXT_ATTR_UNDERLINE;
                underline_active = 1;
            }
            text++;
            continue;
        }

        char buf[2] = {*text, '\0'};
        text_mode_print_at_attr_bg(cur_x, y, buf, fg, bg, attr);
        cur_x++;
        text++;
    }

    if (bold_active) {
        *bold_pending = 1;
    }
    if (underline_active) {
        *underline_pending = 1;
    }
}

void reader_view_draw_reading_page(const reader_state_t *state, int *bold_pending, int *underline_pending) {
    ui_clear();

    int cols = text_mode_get_cols();

    const char *file_name = state->current_file;
    const char *slash = strrchr(file_name, '/');
    if (slash) {
        file_name = slash + 1;
    }

    char page_info[48];
    if (state->total_pages > 0) {
        snprintf(page_info, sizeof(page_info), "Page %d/%d", state->page_number, state->total_pages);
    } else {
        snprintf(page_info, sizeof(page_info), "Page %d", state->page_number);
    }

    for (int x = 0; x < cols; x++) {
        text_mode_print_at_attr_bg(x, 0, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
    }
    text_mode_print_at_attr(1, 0, file_name, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);

    int toc_btn_x = cols - 15;
    int back_btn_x = cols - 7;

    int info_x = toc_btn_x - 1 - (int)strlen(page_info);
    if (info_x > 0) {
        text_mode_print_at_attr(info_x, 0, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    }

    // Create/update reading mode header buttons
    reader_state_t *mutable_state = (reader_state_t*)state;
    if (!mutable_state->btn_jump) {
        mutable_state->btn_jump = ui_button_create(toc_btn_x, 0, 7, 1, "TOC");
        ui_button_set_callback(mutable_state->btn_jump, on_reading_toc_click, mutable_state);

        mutable_state->btn_back = ui_button_create(back_btn_x, 0, 7, 1, "<<<");
        ui_button_set_callback(mutable_state->btn_back, on_reading_back_click, mutable_state);
    } else {
        // Update positions if screen size changed
        mutable_state->btn_jump->x = toc_btn_x;
        mutable_state->btn_jump->y = 0;

        mutable_state->btn_back->x = back_btn_x;
        mutable_state->btn_back->y = 0;
    }

    // Draw header buttons
    ui_button_draw(state->btn_jump);
    ui_button_draw(state->btn_back);

    if (state->search_status[0]) {
        int status_len = (int)strlen(state->search_status);
        if (status_len > cols - 2) {
            status_len = cols - 2;
        }
        char status[96];
        strncpy(status, state->search_status, sizeof(status) - 1);
        status[sizeof(status) - 1] = '\0';
        if ((int)strlen(status) > status_len) {
            status[status_len] = '\0';
        }

        for (int x = 0; x < cols; x++) {
            text_mode_print_at_color(x, 1, " ", TEXT_COLOR_CYAN);
        }
        text_mode_print_at_color(1, 1, status, TEXT_COLOR_CYAN);
    }

    for (int line_index = 0; line_index < state->line_count && line_index < state->content_rows; line_index++) {
        const rendered_line_t *rendered_line = &state->lines[line_index];
        if (rendered_line->text[0] == '\0') {
            *bold_pending = 0;
            *underline_pending = 0;
        }
        draw_rich_line(MARGIN, 2 + line_index, rendered_line->text, rendered_line->color, TEXT_COLOR_BLACK, rendered_line->attr, bold_pending, underline_pending);
    }
}

void reader_view_draw_toc(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int window_height = rows - 3; // Reserve 3 rows for buttons at bottom
    int list_height = window_height - 3; // Account for title (1) and margins (2)

    ui_clear();
    ui_window(0, 0, cols, window_height, "Table of Contents");

    // Create or update TOC list widget
    if (!state->toc_list) {
        state->toc_list = ui_list_create(2, 2, cols - 4, list_height);
        ui_list_set_colors(state->toc_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                           TEXT_COLOR_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui_list_set_border(state->toc_list, true);
        ui_list_set_scrollbar(state->toc_list, true);

        // Set up callbacks
        ui_list_set_callbacks(state->toc_list, on_toc_list_selection_changed,
                              on_toc_list_item_selected, state);
    } else {
        // Update dimensions if screen size changed
        state->toc_list->x = 2;
        state->toc_list->y = 2;
        state->toc_list->width = cols - 4;
        state->toc_list->height = list_height;
    }

    // Update list items if TOC is available
    if (state->toc_count > 0) {
        // Allocate or reallocate titles array if needed
        if (!state->toc_titles) {
            state->toc_titles = (const char **)malloc(sizeof(char *) * state->toc_count);
        }

        if (state->toc_titles) {
            for (int i = 0; i < state->toc_count; i++) {
                state->toc_titles[i] = state->toc[i].title;
            }

            ui_list_set_items(state->toc_list, state->toc_titles, state->toc_count);
            ui_list_set_selection(state->toc_list, state->toc_selected);
        }

        ui_list_draw(state->toc_list);
    } else {
        ui_label(2, 2, "No headings found", TEXT_COLOR_YELLOW);
    }

    int button_row = rows - 3;
    int button_width = FILE_LIST_BTN_WIDTH;

    // Create button widgets
    int up_x = 2;
    int jump_x = up_x + button_width + FILE_LIST_BTN_GAP;
    int down_x = jump_x + button_width + FILE_LIST_BTN_GAP;
    int back_x = down_x + button_width + FILE_LIST_BTN_GAP;

    // Create buttons if they don't exist
    if (!state->btn_up) {
        // Callbacks (defined in reader_events.c)
        state->btn_up = ui_button_create(up_x, button_row, button_width, 3, "UP");
        ui_button_set_callback(state->btn_up, on_toc_up_click, state);

        state->btn_open = ui_button_create(jump_x, button_row, button_width, 3, "JUMP");
        ui_button_set_callback(state->btn_open, on_toc_jump_click, state);

        state->btn_down = ui_button_create(down_x, button_row, button_width, 3, "DOWN");
        ui_button_set_callback(state->btn_down, on_toc_down_click, state);

        state->btn_exit = ui_button_create(back_x, button_row, button_width, 3, "EXIT");
        ui_button_set_callback(state->btn_exit, on_toc_back_click, state);
    } else {
        // Update positions if screen size changed
        state->btn_up->x = up_x;
        state->btn_up->y = button_row;

        state->btn_open->x = jump_x;
        state->btn_open->y = button_row;

        state->btn_down->x = down_x;
        state->btn_down->y = button_row;

        state->btn_exit->x = back_x;
        state->btn_exit->y = button_row;
    }

    // Draw buttons
    ui_button_draw(state->btn_up);
    ui_button_draw(state->btn_open);
    ui_button_draw(state->btn_down);
    ui_button_draw(state->btn_exit);
}

void reader_view_update_toc_selection(const reader_state_t *state, int previous_selected) {
    if (!state->toc_list || state->toc_count == 0) {
        return;
    }

    // Update list widget selection
    ui_list_set_selection(state->toc_list, state->toc_selected);

    // Redraw the list
    ui_list_draw(state->toc_list);
}

void reader_view_draw_file_list(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_rows = rows - 5;

    ui_clear();
    ui_window(0, 0, cols, rows - 3, "Select a Book"); // Make room for buttons
    ui_menu_draw(1, 1, list_rows, state->file_ptrs, state->file_count, state->file_selected);

    int button_row = rows - 3;
    int button_width = FILE_LIST_BTN_WIDTH;

    int up_x = 2;
    int open_x = up_x + button_width + FILE_LIST_BTN_GAP;
    int down_x = open_x + button_width + FILE_LIST_BTN_GAP;
    int exit_x = down_x + button_width + FILE_LIST_BTN_GAP;

    // Create buttons if they don't exist
    if (!state->btn_up) {
        state->btn_up = ui_button_create(up_x, button_row, button_width, 3, "UP");
        ui_button_set_callback(state->btn_up, on_file_list_up_click, state);

        state->btn_open = ui_button_create(open_x, button_row, button_width, 3, "OPEN");
        ui_button_set_callback(state->btn_open, on_file_list_open_click, state);

        state->btn_down = ui_button_create(down_x, button_row, button_width, 3, "DOWN");
        ui_button_set_callback(state->btn_down, on_file_list_down_click, state);

        state->btn_exit = ui_button_create(exit_x, button_row, button_width, 3, "EXIT");
        ui_button_set_callback(state->btn_exit, on_file_list_exit_click, state);
    } else {
        // Update positions if screen size changed
        state->btn_up->x = up_x;
        state->btn_up->y = button_row;

        state->btn_open->x = open_x;
        state->btn_open->y = button_row;

        state->btn_down->x = down_x;
        state->btn_down->y = button_row;

        state->btn_exit->x = exit_x;
        state->btn_exit->y = button_row;
    }

    // Draw buttons
    ui_button_draw(state->btn_up);
    ui_button_draw(state->btn_open);
    ui_button_draw(state->btn_down);
    ui_button_draw(state->btn_exit);
}
