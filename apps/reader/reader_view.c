#include "reader_view.h"

#include "reader_events.h"
#include "text_mode.h"
#include "ui.h"
#include "hardware.h"

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
        attr |= TEXT_ATTR_ITALIC;
        underline_active = 1;
        *underline_pending = 0;
    }

    while (*text) {
        if (*text == MD_FORMAT_UNDERLINE) {
            if (attr & TEXT_ATTR_UNDERLINE) {
                attr &= ~TEXT_ATTR_UNDERLINE;
            } else {
                attr |= TEXT_ATTR_UNDERLINE;
            }
            text++;
            continue;
        }
        if (*text == MD_FORMAT_BOLD) {
            if (attr & TEXT_ATTR_BOLD) {
                attr &= ~TEXT_ATTR_BOLD;
                bold_active = 0;
            } else {
                attr |= TEXT_ATTR_BOLD;
                bold_active = 1;
            }
            text++;
            continue;
        }
        if (*text == MD_FORMAT_TOGGLE) {
            if (attr & TEXT_ATTR_ITALIC) {
                attr &= ~TEXT_ATTR_ITALIC;
                underline_active = 0;
            } else {
                attr |= TEXT_ATTR_ITALIC;
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
    // Update screen dimensions to detect cache invalidation
    reader_state_t *mutable_state = (reader_state_t*)state;
    mutable_state->screen_width = text_mode_get_cols() - MARGIN * 2;
    int current_rows = text_mode_get_rows();
    bool currently_portrait = display_get_height() >= display_get_width();
    if (currently_portrait) {
        mutable_state->content_rows = current_rows - 4;  // Account for top 2 rows and bottom 2 rows in portrait
    } else {
        mutable_state->content_rows = current_rows - 2;  // Account for top 2 rows in landscape
    }

    ui_clear();

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    const char *file_name = state->current_file;
    const char *slash = strrchr(file_name, '/');
    if (slash) {
        file_name = slash + 1;
    }

    // Check if this is a markdown file and show title without extension
    const char *display_name = file_name;
    char temp_name[256];
    if (strlen(file_name) > 3 && strcmp(file_name + strlen(file_name) - 3, ".md") == 0) {
        strncpy(temp_name, file_name, sizeof(temp_name) - 1);
        temp_name[sizeof(temp_name) - 1] = '\0';
        // Remove .md extension
        if (strlen(temp_name) > 3) {
            temp_name[strlen(temp_name) - 3] = '\0';
        }
        display_name = temp_name;
    }

    char page_info[48];
    if (state->total_pages > 0) {
        snprintf(page_info, sizeof(page_info), "Page %d/%d", state->page_number, state->total_pages);
    } else {
        snprintf(page_info, sizeof(page_info), "Page %d", state->page_number);
    }

    // Responsive layout based on screen orientation (pixel dimensions)
    bool is_portrait = display_get_height() >= display_get_width();
    
    if (is_portrait) {
        // Portrait mode: top row just for book name, bottom row for controls
        
        // Top row - book name only
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_attr_bg(x, 0, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
        }
        text_mode_print_at_attr(1, 0, display_name, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);
        
        // Draw content starting from row 2 (row 0 is title, row 1 is empty for spacing)
        int content_start_row = 2;
        int content_rows_available = rows - 4;  // Leave 1 empty row above bottom bar
        
        // Search status display
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
                text_mode_print_at_color(x, content_start_row, " ", TEXT_COLOR_CYAN);
            }
            text_mode_print_at_color(1, content_start_row, status, TEXT_COLOR_CYAN);
            content_start_row++;
            content_rows_available--;
        }

        // Bottom area - progress and buttons (actual bottom row)
        int bottom_row = rows - 1;
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_attr_bg(x, bottom_row, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_BORDER_TOP);
        }
        
        // Bottom row - progress info
        int info_x = 1;
        text_mode_print_at_attr(info_x, bottom_row, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_BORDER_TOP);
        
        // Bottom row buttons - TOC, Find, Goto, Back
        int btn_width = 5;
        int btn_gap = 0;
        int num_buttons = 4;
        int total_btn_width = (btn_width * num_buttons) + (btn_gap * (num_buttons - 1));

        // Position buttons from right to left
        int back_btn_x = cols - btn_width - 1;
        int goto_btn_x = back_btn_x - btn_width - btn_gap;
        int find_btn_x = goto_btn_x - btn_width - btn_gap;
        int toc_btn_x = find_btn_x - btn_width - btn_gap;

        // Create/update reading mode buttons at bottom
        reader_state_t *mutable_state = (reader_state_t*)state;
        if (!mutable_state->btn_jump) {
            mutable_state->btn_jump = ui_button_create(toc_btn_x, bottom_row, btn_width, 1, "TOC");
            ui_button_set_callback(mutable_state->btn_jump, on_reading_toc_click, mutable_state);

            mutable_state->btn_find = ui_button_create(find_btn_x, bottom_row, btn_width, 1, "Find");
            ui_button_set_callback(mutable_state->btn_find, on_reading_find_click, mutable_state);

            mutable_state->btn_goto = ui_button_create(goto_btn_x, bottom_row, btn_width, 1, "Goto");
            ui_button_set_callback(mutable_state->btn_goto, on_reading_goto_click, mutable_state);

            mutable_state->btn_back = ui_button_create(back_btn_x, bottom_row, btn_width, 1, "Back");
            ui_button_set_callback(mutable_state->btn_back, on_reading_back_click, mutable_state);
        } else {
            // Update positions if screen size changed
            mutable_state->btn_jump->x = toc_btn_x;
            mutable_state->btn_jump->y = bottom_row;
            mutable_state->btn_jump->width = btn_width;

            mutable_state->btn_find->x = find_btn_x;
            mutable_state->btn_find->y = bottom_row;
            mutable_state->btn_find->width = btn_width;

            mutable_state->btn_goto->x = goto_btn_x;
            mutable_state->btn_goto->y = bottom_row;
            mutable_state->btn_goto->width = btn_width;

            mutable_state->btn_back->x = back_btn_x;
            mutable_state->btn_back->y = bottom_row;
            mutable_state->btn_back->width = btn_width;
        }

        // Draw buttons at bottom
        ui_button_draw(state->btn_jump);
        ui_button_draw(state->btn_find);
        ui_button_draw(state->btn_goto);
        ui_button_draw(state->btn_back);
        
        // Draw content
        for (int line_index = 0; line_index < state->line_count && line_index < content_rows_available; line_index++) {
            const rendered_line_t *rendered_line = &state->lines[line_index];
            if (rendered_line->text[0] == '\0') {
                *bold_pending = 0;
                *underline_pending = 0;
            }
            draw_rich_line(MARGIN, content_start_row + line_index, rendered_line->text, rendered_line->color, TEXT_COLOR_BLACK, rendered_line->attr, bold_pending, underline_pending);
        }
        
    } else {
        // Landscape mode - keep current layout
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_attr_bg(x, 0, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
        }
        text_mode_print_at_attr(1, 0, display_name, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);

        // Calculate header button positions dynamically
        int btn_width = 5;
        int btn_gap = 0;
        int num_buttons = 4;

        int back_btn_x = cols - btn_width - 1;
        int goto_btn_x = back_btn_x - btn_width - btn_gap;
        int find_btn_x = goto_btn_x - btn_width - btn_gap;
        int toc_btn_x = find_btn_x - btn_width - btn_gap;

        int info_x = toc_btn_x - 1 - (int)strlen(page_info);
        if (info_x > 0) {
            text_mode_print_at_attr(info_x, 0, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
        }

        // Create/update reading mode header buttons
        reader_state_t *mutable_state = (reader_state_t*)state;
        if (!mutable_state->btn_jump) {
            mutable_state->btn_jump = ui_button_create(toc_btn_x, 0, btn_width, 1, "TOC");
            ui_button_set_callback(mutable_state->btn_jump, on_reading_toc_click, mutable_state);

            mutable_state->btn_find = ui_button_create(find_btn_x, 0, btn_width, 1, "Find");
            ui_button_set_callback(mutable_state->btn_find, on_reading_find_click, mutable_state);

            mutable_state->btn_goto = ui_button_create(goto_btn_x, 0, btn_width, 1, "Goto");
            ui_button_set_callback(mutable_state->btn_goto, on_reading_goto_click, mutable_state);

            mutable_state->btn_back = ui_button_create(back_btn_x, 0, btn_width, 1, "Back");
            ui_button_set_callback(mutable_state->btn_back, on_reading_back_click, mutable_state);
        } else {
            // Update positions if screen size changed
            mutable_state->btn_jump->x = toc_btn_x;
            mutable_state->btn_jump->y = 0;
            mutable_state->btn_jump->width = btn_width;

            mutable_state->btn_find->x = find_btn_x;
            mutable_state->btn_find->y = 0;
            mutable_state->btn_find->width = btn_width;

            mutable_state->btn_goto->x = goto_btn_x;
            mutable_state->btn_goto->y = 0;
            mutable_state->btn_goto->width = btn_width;

            mutable_state->btn_back->x = back_btn_x;
            mutable_state->btn_back->y = 0;
            mutable_state->btn_back->width = btn_width;
        }

        // Draw header buttons
        ui_button_draw(state->btn_jump);
        ui_button_draw(state->btn_find);
        ui_button_draw(state->btn_goto);
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
}

void reader_view_draw_toc(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_height = rows - 5; // Reserve space for title, borders, and button row (3 rows)

    ui_clear();
    reader_view_clear_widgets(state); // Clean up any existing widgets

    // Create or update TOC list widget
    if (!state->toc_list) {
        state->toc_list = ui_list_create(1, 1, cols - 2, list_height);
        ui_list_set_title(state->toc_list, "Table of Contents");
        ui_list_set_colors(state->toc_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                           TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui_list_set_border(state->toc_list, true);
        ui_list_set_scrollbar(state->toc_list, true);

        // Set up callbacks
        ui_list_set_callbacks(state->toc_list, on_toc_list_selection_changed,
                              on_toc_list_item_selected, state);
    } else {
        // Update dimensions if screen size changed
        state->toc_list->x = 1;
        state->toc_list->y = 1;
        state->toc_list->width = cols - 2;
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

    // Fixed button layout - use specific widths that fit the text
    // Button text widths: "UP"=2, "JUMP"=4, "DOWN"=4, "EXIT"=4
    // Add 2 spaces padding on each side = +4 per button
    // Total widths: UP=6, JUMP=8, DOWN=8, EXIT=8
    // Single column gap between buttons
    int up_width = 6;
    int jump_width = 8;
    int down_width = 8;
    int back_width = 8;
    int gap = 1;

    int total_width = up_width + gap + jump_width + gap + down_width + gap + back_width;
    int start_x = (cols - total_width) / 2;
    if (start_x < 1) start_x = 1;

    // Calculate button positions
    int up_x = start_x;
    int jump_x = up_x + up_width + gap;
    int down_x = jump_x + jump_width + gap;
    int back_x = down_x + down_width + gap;

    // Create buttons if they don't exist
    if (!state->btn_up) {
        // Callbacks (defined in reader_events.c)
        state->btn_up = ui_button_create(up_x, button_row, up_width, 3, "UP");
        ui_button_set_callback(state->btn_up, on_toc_up_click, state);

        state->btn_open = ui_button_create(jump_x, button_row, jump_width, 3, "JUMP");
        ui_button_set_callback(state->btn_open, on_toc_jump_click, state);

        state->btn_down = ui_button_create(down_x, button_row, down_width, 3, "DOWN");
        ui_button_set_callback(state->btn_down, on_toc_down_click, state);

        state->btn_exit = ui_button_create(back_x, button_row, back_width, 3, "EXIT");
        ui_button_set_callback(state->btn_exit, on_toc_back_click, state);
    } else {
        // Update positions and sizes if screen size changed
        state->btn_up->x = up_x;
        state->btn_up->y = button_row;
        state->btn_up->width = up_width;

        state->btn_open->x = jump_x;
        state->btn_open->y = button_row;
        state->btn_open->width = jump_width;

        state->btn_down->x = down_x;
        state->btn_down->y = button_row;
        state->btn_down->width = down_width;

        state->btn_exit->x = back_x;
        state->btn_exit->y = button_row;
        state->btn_exit->width = back_width;
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

void reader_view_draw_receiving(const reader_state_t *state) {
    ui_clear();
    ui_window(2, 2, 60, 10, "Receiving File");

    int cols = text_mode_get_cols();
    int center_x = cols / 2;

    text_mode_print_at_attr(center_x - 14, 5, "Waiting for file transfer...", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(center_x - 12, 7, "Use WebSerial to send a .md file", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(center_x - 6, 9, "ESC to cancel", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
}

void reader_view_clear_widgets(reader_state_t *state) {
    // Destroy all button widgets
    if (state->btn_up) {
        free(state->btn_up);
        state->btn_up = NULL;
    }
    if (state->btn_get) {
        free(state->btn_get);
        state->btn_get = NULL;
    }
    if (state->btn_open) {
        free(state->btn_open);
        state->btn_open = NULL;
    }
    if (state->btn_down) {
        free(state->btn_down);
        state->btn_down = NULL;
    }
    if (state->btn_exit) {
        free(state->btn_exit);
        state->btn_exit = NULL;
    }
    if (state->btn_jump) {
        free(state->btn_jump);
        state->btn_jump = NULL;
    }
    if (state->btn_back) {
        free(state->btn_back);
        state->btn_back = NULL;
    }

    // Destroy list widgets
    if (state->toc_list) {
        ui_list_destroy(state->toc_list);
        state->toc_list = NULL;
    }
    if (state->file_list) {
        ui_list_destroy(state->file_list);
        state->file_list = NULL;
    }
}

void reader_view_draw_file_list(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_height = rows - 5; // Account for title (1), borders (2), and button row (3)

    ui_clear();
    reader_view_clear_widgets(state); // Clean up any existing widgets

    // Create or update file list widget
    if (!state->file_list) {
        state->file_list = ui_list_create(1, 1, cols - 2, list_height);
        ui_list_set_title(state->file_list, "Select a Book");
        ui_list_set_colors(state->file_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                           TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui_list_set_border(state->file_list, true);
        ui_list_set_scrollbar(state->file_list, true);

        // Set up callbacks
        ui_list_set_callbacks(state->file_list, on_file_list_selection_changed,
                              on_file_list_item_selected, state);
    } else {
        // Update dimensions if screen size changed
        state->file_list->x = 1;
        state->file_list->y = 1;
        state->file_list->width = cols - 2;
        state->file_list->height = list_height;
    }

    // Update list items if files are available
    if (state->file_count > 0) {
        ui_list_set_items(state->file_list, state->file_ptrs, state->file_count);
        ui_list_set_selection(state->file_list, state->file_selected);
        ui_list_draw(state->file_list);
    } else {
        ui_label(2, 2, "No books found", TEXT_COLOR_YELLOW);
    }

    int button_row = rows - 3;

    // Fixed button layout - use specific widths that fit the text
    // Order: UP, DOWN, OPEN, GET, EXIT
    // Button text widths: "UP"=2, "DOWN"=4, "OPEN"=4, "GET"=3, "EXIT"=4
    // Add 2 spaces padding on each side = +4 per button
    // Total widths: UP=6, DOWN=8, OPEN=8, GET=7, EXIT=8
    // Single column gap between buttons
    int up_width = 6;
    int down_width = 8;
    int open_width = 8;
    int get_width = 7;
    int exit_width = 8;
    int gap = 1;

    int total_width = up_width + gap + down_width + gap + open_width + gap + get_width + gap + exit_width;
    int start_x = (cols - total_width) / 2;
    if (start_x < 1) start_x = 1;

    int up_x = start_x;
    int down_x = up_x + up_width + gap;
    int open_x = down_x + down_width + gap;
    int get_x = open_x + open_width + gap;
    int exit_x = get_x + get_width + gap;

    // Create buttons if they don't exist
    if (!state->btn_up) {
        state->btn_up = ui_button_create(up_x, button_row, up_width, 3, "UP");
        ui_button_set_callback(state->btn_up, on_file_list_up_click, state);

        state->btn_down = ui_button_create(down_x, button_row, down_width, 3, "DOWN");
        ui_button_set_callback(state->btn_down, on_file_list_down_click, state);

        state->btn_open = ui_button_create(open_x, button_row, open_width, 3, "OPEN");
        ui_button_set_callback(state->btn_open, on_file_list_open_click, state);

        state->btn_get = ui_button_create(get_x, button_row, get_width, 3, "GET");
        ui_button_set_callback(state->btn_get, on_file_list_get_click, state);

        state->btn_exit = ui_button_create(exit_x, button_row, exit_width, 3, "EXIT");
        ui_button_set_callback(state->btn_exit, on_file_list_exit_click, state);
    } else {
        // Update positions and sizes if screen size changed
        state->btn_up->x = up_x;
        state->btn_up->y = button_row;
        state->btn_up->width = up_width;

        state->btn_down->x = down_x;
        state->btn_down->y = button_row;
        state->btn_down->width = down_width;

        state->btn_open->x = open_x;
        state->btn_open->y = button_row;
        state->btn_open->width = open_width;

        state->btn_get->x = get_x;
        state->btn_get->y = button_row;
        state->btn_get->width = get_width;

        state->btn_exit->x = exit_x;
        state->btn_exit->y = button_row;
        state->btn_exit->width = exit_width;
    }

    // Draw buttons
    ui_button_draw(state->btn_up);
    ui_button_draw(state->btn_down);
    ui_button_draw(state->btn_open);
    ui_button_draw(state->btn_get);
    ui_button_draw(state->btn_exit);
}
