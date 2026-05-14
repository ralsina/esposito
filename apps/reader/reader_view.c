#include "reader_view.h"

#include "reader_md.h"
#include "text_mode.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define FILE_LIST_BTN_WIDTH 13
#define FILE_LIST_BTN_GAP 2
#define FILE_LIST_BTN_UP_LABEL "  UP  "
#define FILE_LIST_BTN_OPEN_LABEL " OPEN "
#define FILE_LIST_BTN_DOWN_LABEL " DOWN "
#define FILE_LIST_BTN_EXIT_LABEL " EXIT "

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

    int info_x = cols - 3 - (int)strlen(page_info);
    if (info_x > 0) {
        text_mode_print_at_attr(info_x, 0, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    }
    text_mode_print_at_attr_bg(cols - 2, 0, "<-", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_INVERSE);

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
    int list_rows = rows - 4;

    ui_clear();
    ui_window(0, 0, cols, rows, "Table of Contents");

    if (state->toc_count == 0) {
        ui_label(2, 2, "No headings found", TEXT_COLOR_YELLOW);
    } else {
        // Scroll window so selected entry is visible
        int scroll = 0;
        if (state->toc_selected >= list_rows) {
            scroll = state->toc_selected - list_rows + 1;
        }

        for (int i = 0; i < list_rows && (i + scroll) < state->toc_count; i++) {
            int idx = i + scroll;
            const toc_entry_t *entry = &state->toc[idx];
            char marker = (idx == state->toc_selected) ? '>' : ' ';
            char pg[8];
            snprintf(pg, sizeof(pg), "p.%d", entry->page_number);
            int pg_len = (int)strlen(pg);
            int title_max = cols - 6 - pg_len;

            char title[64];
            strncpy(title, entry->title, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
            if ((int)strlen(title) > title_max) {
                title[title_max - 1] = '.';
                title[title_max] = '\0';
            }

            uint8_t color = (idx == state->toc_selected) ? TEXT_COLOR_GREEN : TEXT_COLOR_WHITE;
            char line[80];
            snprintf(line, sizeof(line), "%c %s", marker, title);
            text_mode_print_at_color(2, 2 + i, line, color);
            text_mode_print_at_color(cols - 2 - pg_len, 2 + i, pg, TEXT_COLOR_CYAN);
        }
    }

    ui_status_bar(rows - 2, "W/S Navigate  Enter Jump", "ESC Cancel");
}

void reader_view_draw_file_list(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_rows = rows - 5;

    ui_clear();
    ui_window(0, 0, cols, rows, "Select a Book");
    ui_menu_draw(1, 1, list_rows, state->file_ptrs, state->file_count, state->file_selected);

    int button_row = rows - 3;
    int button_width = FILE_LIST_BTN_WIDTH;

    char spacer[64];
    memset(spacer, ' ', button_width);
    spacer[button_width] = '\0';

    int up_x = 2;
    int open_x = up_x + button_width + FILE_LIST_BTN_GAP;
    int down_x = open_x + button_width + FILE_LIST_BTN_GAP;
    int exit_x = down_x + button_width + FILE_LIST_BTN_GAP;

    text_mode_print_at_attr_bg(up_x, button_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(up_x + (button_width - 6) / 2, button_row, FILE_LIST_BTN_UP_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(open_x, button_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(open_x + (button_width - 6) / 2, button_row, FILE_LIST_BTN_OPEN_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(down_x, button_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(down_x + (button_width - 6) / 2, button_row, FILE_LIST_BTN_DOWN_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr_bg(exit_x, button_row, spacer, TEXT_COLOR_BLACK, TEXT_COLOR_RED, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(exit_x + (button_width - 6) / 2, button_row, FILE_LIST_BTN_EXIT_LABEL, TEXT_COLOR_BLACK, TEXT_COLOR_RED, TEXT_ATTR_NORMAL);

    state->btn_up_x = up_x;
    state->btn_open_x = open_x;
    state->btn_down_x = down_x;
    state->btn_exit_x = exit_x;
    state->btn_row = button_row;
    state->btn_w = button_width;
}
