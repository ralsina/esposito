#include "reader_view.h"

#include "reader_core.h"
#include "reader_events.h"
#include "reader_nav.h"
#include "text_mode.h"
#include "ui2.h"
#include "ui2_button.h"
#include "ui2_list.h"
#include "ui2_text_input.h"
#include "ui2_layout.h"
#include "lucide_icons.h"
#include "hardware.h"
#include "ui2_osk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void draw_window(int x, int y, int w, int h, const char *title) {
    int x2 = x + w - 1;
    int y2 = y + h - 1;

    for (int cx = x; cx <= x2; cx++) {
        text_mode_print_at_attr(cx, y, "\xE2\x94\x80", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_print_at_attr(cx, y2, "\xE2\x94\x80", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    }
    for (int cy = y; cy <= y2; cy++) {
        text_mode_print_at_attr(x, cy, "\xE2\x94\x82", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_print_at_attr(x2, cy, "\xE2\x94\x82", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    }
    text_mode_print_at_attr(x, y, "\xE2\x94\x8C", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(x2, y, "\xE2\x94\x90", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(x, y2, "\xE2\x94\x94", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(x2, y2, "\xE2\x94\x98", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    if (title && title[0]) {
        int tlen = (int)strlen(title);
        int tx = x + (w - tlen) / 2;
        if (tx < x) tx = x + 1;
        text_mode_print_at_attr(tx, y, title, TEXT_COLOR_CYAN, TEXT_ATTR_BOLD);
    }
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
            if (attr & TEXT_ATTR_UNDERLINE) attr &= ~TEXT_ATTR_UNDERLINE;
            else attr |= TEXT_ATTR_UNDERLINE;
            text++;
            continue;
        }
        if (*text == MD_FORMAT_BOLD) {
            if (attr & TEXT_ATTR_BOLD) { attr &= ~TEXT_ATTR_BOLD; bold_active = 0; }
            else { attr |= TEXT_ATTR_BOLD; bold_active = 1; }
            text++;
            continue;
        }
        if (*text == MD_FORMAT_TOGGLE) {
            if (attr & TEXT_ATTR_ITALIC) { attr &= ~TEXT_ATTR_ITALIC; underline_active = 0; }
            else { attr |= TEXT_ATTR_ITALIC; underline_active = 1; }
            text++;
            continue;
        }

        int utf8_len = 1;
        if ((*text & 0xE0) == 0xC0) utf8_len = 2;
        else if ((*text & 0xF0) == 0xE0) utf8_len = 3;
        char buf[4] = {0};
        for (int i = 0; i < utf8_len && text[i]; i++) buf[i] = text[i];
        text_mode_print_at_attr_bg(cur_x, y, buf, fg, bg, attr);
        cur_x++;
        text += utf8_len;
    }

    if (bold_active) *bold_pending = 1;
    if (underline_active) *underline_pending = 1;
}

static void null_reading_buttons(reader_state_t *state) {
    state->btn_jump = NULL;
    state->btn_find = NULL;
    state->btn_goto = NULL;
    state->btn_back = NULL;
    state->btn_cancel = NULL;
}

void reader_view_setup_file_list(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_height = rows - 5;

    null_reading_buttons(state);

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_VERTICAL);
    ui2_layout_set_gap(root, 1);

    ui2_list_t *list = ui2_list_create(1, 0, cols - 2, list_height);
    ui2_list_set_title(list, "Select a Book");
    ui2_list_set_colors(list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                       TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
    ui2_list_set_border(list, true);
    ui2_list_set_callbacks(list, on_file_list_selection_changed,
                          on_file_list_item_activated, state);

    if (state->file_count > 0) {
        ui2_list_set_items(list, state->file_ptrs, state->file_count);
        ui2_list_set_selection(list, state->file_selected);
    }

    ui2_layout_add(root, UI2_WIDGET(list));

    ui2_layout_t *btn_row = ui2_layout_create(0, 0, cols, 3, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(btn_row, 1);

     int btn_w = 5;
    ui2_button_t *btn_up = ui2_button_create(0, 0, btn_w, 3, ICON_CHEVRON_UP);
    ui2_button_set_callback(btn_up, on_file_list_up_click, list);

    ui2_button_t *btn_down = ui2_button_create(0, 0, btn_w, 3, ICON_CHEVRON_DOWN);
    ui2_button_set_callback(btn_down, on_file_list_down_click, list);

    ui2_button_t *btn_open = ui2_button_create(0, 0, btn_w, 3, ICON_DOWNLOAD);
    ui2_button_set_callback(btn_open, on_file_list_open_click, state);

    ui2_button_t *btn_get = ui2_button_create(0, 0, btn_w, 3, ICON_UPLOAD);
    ui2_button_set_callback(btn_get, on_file_list_get_click, state);

    ui2_button_t *btn_exit = ui2_button_create(0, 0, btn_w, 3, ICON_X_CIRCLE);
    ui2_button_set_callback(btn_exit, on_file_list_exit_click, state);

     ui2_layout_add(btn_row, UI2_WIDGET(btn_up));
     ui2_layout_add(btn_row, UI2_WIDGET(btn_down));
     ui2_layout_add(btn_row, UI2_WIDGET(btn_open));
     ui2_layout_add(btn_row, UI2_WIDGET(btn_get));
     ui2_layout_add(btn_row, UI2_WIDGET(btn_exit));

    ui2_layout_add(root, UI2_WIDGET(btn_row));

    ui2_screen_set_root(state->screen, root);
    ui2_screen_focus_set(state->screen, UI2_WIDGET(list));
}

void reader_view_setup_reading(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    bool is_portrait = display_get_height() >= display_get_width();

    null_reading_buttons(state);

    int btn_width = 3;
    int btn_gap = 1;
    int back_btn_x = cols - btn_width - 1;
    int goto_btn_x = back_btn_x - btn_width - btn_gap;
    int find_btn_x = goto_btn_x - btn_width - btn_gap;
    int toc_btn_x = find_btn_x - btn_width - btn_gap;

    int btn_row;
    if (is_portrait) {
        btn_row = rows - 1;
    } else {
        btn_row = 0;
    }

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);

    state->btn_jump = ui2_button_create(toc_btn_x, btn_row, btn_width, 1, ICON_BOOK_OPEN);
    ui2_button_set_callback(state->btn_jump, on_reading_toc_click, state);

    state->btn_find = ui2_button_create(find_btn_x, btn_row, btn_width, 1, ICON_SEARCH);
    ui2_button_set_callback(state->btn_find, on_reading_find_click, state);

    state->btn_goto = ui2_button_create(goto_btn_x, btn_row, btn_width, 1, ICON_ARROW_RIGHT);
    ui2_button_set_callback(state->btn_goto, on_reading_goto_click, state);

    state->btn_back = ui2_button_create(back_btn_x, btn_row, btn_width, 1, ICON_ARROW_LEFT);
    ui2_button_set_callback(state->btn_back, on_reading_back_click, state);

    ui2_layout_add(root, UI2_WIDGET(state->btn_jump));
    ui2_layout_add(root, UI2_WIDGET(state->btn_find));
    ui2_layout_add(root, UI2_WIDGET(state->btn_goto));
    ui2_layout_add(root, UI2_WIDGET(state->btn_back));

    ui2_screen_set_root(state->screen, root);
}

void reader_view_render_reading(reader_state_t *state, int *bold_pending, int *underline_pending) {
    reader_state_t *mutable_state = (reader_state_t *)state;
    mutable_state->screen_width = text_mode_get_cols() - MARGIN * 2;
    int current_rows = text_mode_get_rows();
    bool currently_portrait = display_get_height() >= display_get_width();
    if (currently_portrait) {
        mutable_state->content_rows = current_rows - 4;
    } else {
        mutable_state->content_rows = current_rows - 2;
    }

    if (!reader_alloc_lines(state, state->screen_width, state->content_rows)) {
        printf("VIEW: Failed to allocate lines buffer!\n");
        return;
    }

    text_mode_clear(TEXT_COLOR_BLACK);

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    const char *file_name = state->current_file;
    const char *slash = strrchr(file_name, '/');
    if (slash) file_name = slash + 1;

    const char *display_name = file_name;
    char temp_name[256];
    if (strlen(file_name) > 3 && strcmp(file_name + strlen(file_name) - 3, ".md") == 0) {
        strncpy(temp_name, file_name, sizeof(temp_name) - 1);
        temp_name[sizeof(temp_name) - 1] = '\0';
        if (strlen(temp_name) > 3) temp_name[strlen(temp_name) - 3] = '\0';
        display_name = temp_name;
    }

    char page_info[48];
    if (state->total_pages > 0) {
        snprintf(page_info, sizeof(page_info), "Page %d/%d", state->page_number, state->total_pages);
    } else {
        snprintf(page_info, sizeof(page_info), "Page %d", state->page_number);
    }

    bool is_portrait = display_get_height() >= display_get_width();

    int btn_width = 3;
    int btn_gap = 1;
    int back_btn_x = cols - btn_width - 1;
    int goto_btn_x = back_btn_x - btn_width - btn_gap;
    int find_btn_x = goto_btn_x - btn_width - btn_gap;
    int toc_btn_x = find_btn_x - btn_width - btn_gap;

    if (is_portrait) {
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_attr_bg(x, 0, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
        }
        text_mode_print_at_attr(1, 0, display_name, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);

        int content_start_row = 2;
        int content_rows_available = rows - 4;

        if (state->search_status[0]) {
            int status_len = (int)strlen(state->search_status);
            if (status_len > cols - 2) status_len = cols - 2;
            char status[96];
            strncpy(status, state->search_status, sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
            if ((int)strlen(status) > status_len) status[status_len] = '\0';
            for (int x = 0; x < cols; x++) {
                text_mode_print_at_color(x, content_start_row, " ", TEXT_COLOR_CYAN);
            }
            text_mode_print_at_color(1, content_start_row, status, TEXT_COLOR_CYAN);
            content_start_row++;
            content_rows_available--;
        }

        int bottom_row = rows - 1;
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_attr_bg(x, bottom_row, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_BORDER_TOP);
        }
        text_mode_print_at_attr(1, bottom_row, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_BORDER_TOP);

        for (int line_index = 0; line_index < state->line_count && line_index < content_rows_available; line_index++) {
            const rendered_line_t *rendered_line = &state->lines[line_index];
            if (rendered_line->text[0] == '\0') { *bold_pending = 0; *underline_pending = 0; }
            draw_rich_line(MARGIN, content_start_row + line_index, rendered_line->text,
                           rendered_line->color, TEXT_COLOR_BLACK, rendered_line->attr, bold_pending, underline_pending);
        }
    } else {
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_attr_bg(x, 0, " ", TEXT_COLOR_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
        }
        text_mode_print_at_attr(1, 0, display_name, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);

        int info_x = toc_btn_x - 1 - (int)strlen(page_info);
        if (info_x > 0) {
            text_mode_print_at_attr(info_x, 0, page_info, TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
        }

        if (state->search_status[0]) {
            int status_len = (int)strlen(state->search_status);
            if (status_len > cols - 2) status_len = cols - 2;
            char status[96];
            strncpy(status, state->search_status, sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
            if ((int)strlen(status) > status_len) status[status_len] = '\0';
            for (int x = 0; x < cols; x++) {
                text_mode_print_at_color(x, 1, " ", TEXT_COLOR_CYAN);
            }
            text_mode_print_at_color(1, 1, status, TEXT_COLOR_CYAN);
        }

        for (int line_index = 0; line_index < state->line_count && line_index < state->content_rows; line_index++) {
            const rendered_line_t *rendered_line = &state->lines[line_index];
            if (rendered_line->text[0] == '\0') { *bold_pending = 0; *underline_pending = 0; }
            draw_rich_line(MARGIN, 2 + line_index, rendered_line->text,
                           rendered_line->color, TEXT_COLOR_BLACK, rendered_line->attr, bold_pending, underline_pending);
        }
    }

    // Draw buttons on top of content
    if (state->btn_jump) UI2_WIDGET(state->btn_jump)->vtable->draw(UI2_WIDGET(state->btn_jump));
    if (state->btn_find) UI2_WIDGET(state->btn_find)->vtable->draw(UI2_WIDGET(state->btn_find));
    if (state->btn_goto) UI2_WIDGET(state->btn_goto)->vtable->draw(UI2_WIDGET(state->btn_goto));
    if (state->btn_back) UI2_WIDGET(state->btn_back)->vtable->draw(UI2_WIDGET(state->btn_back));

    text_mode_flush();
}

void reader_view_setup_toc(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_height = rows - 5;

    null_reading_buttons(state);

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_VERTICAL);
    ui2_layout_set_gap(root, 1);

    ui2_list_t *list = ui2_list_create(1, 0, cols - 2, list_height);
    ui2_list_set_title(list, "Table of Contents");
    ui2_list_set_colors(list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                       TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
    ui2_list_set_border(list, true);
    ui2_list_set_callbacks(list, on_toc_list_selection_changed,
                          on_toc_list_item_activated, state);

    if (state->toc_count > 0) {
        if (!state->toc_titles) {
            state->toc_titles = (const char **)malloc(sizeof(char *) * state->toc_count);
        }
        if (state->toc_titles) {
            for (int i = 0; i < state->toc_count; i++) {
                state->toc_titles[i] = state->toc[i].title;
            }
            ui2_list_set_items(list, state->toc_titles, state->toc_count);
            ui2_list_set_selection(list, state->toc_selected);
        }
    }

    ui2_layout_add(root, UI2_WIDGET(list));

    ui2_layout_t *btn_row = ui2_layout_create(0, 0, cols, 3, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(btn_row, 1);

    ui2_button_t *btn_up = ui2_button_create(0, 0, 6, 3, ICON_CHEVRON_UP);
    ui2_button_set_callback(btn_up, on_toc_up_click, list);

    ui2_button_t *btn_jump = ui2_button_create(0, 0, 8, 3, ICON_ARROW_RIGHT);
    ui2_button_set_callback(btn_jump, on_toc_jump_click, state);

    ui2_button_t *btn_down = ui2_button_create(0, 0, 8, 3, ICON_CHEVRON_DOWN);
    ui2_button_set_callback(btn_down, on_toc_down_click, list);

    ui2_button_t *btn_back = ui2_button_create(0, 0, 8, 3, ICON_X_CIRCLE);
    ui2_button_set_callback(btn_back, on_toc_back_click, state);

    ui2_layout_add(btn_row, UI2_WIDGET(btn_up));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_jump));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_down));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_back));

    ui2_layout_add(root, UI2_WIDGET(btn_row));

    ui2_screen_set_root(state->screen, root);
    ui2_screen_focus_set(state->screen, UI2_WIDGET(list));
}

void reader_view_setup_goto(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();

    null_reading_buttons(state);

    if (ui2_osk_is_active()) {
        return;
    }

    state->mode = MODE_GOTO;
    state->goto_buf[0] = '\0';

    if (!keyboard_is_available()) {
        if (ui2_osk_input_text("Go to Page:", state->goto_buf, sizeof(state->goto_buf), NULL, false)) {
            return;
        }
        state->mode = MODE_READING;
        return;
    }

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);

    state->goto_widget = ui2_text_input_create(0, rows - 4, cols, 4);
    ui2_text_input_set_title(state->goto_widget, "Go to Page");
    ui2_text_input_set_label(state->goto_widget, "Page:");
    ui2_text_input_set_hints(state->goto_widget, "Type number  Enter Confirm", "ESC Cancel");
    ui2_text_input_set_callbacks(state->goto_widget, on_goto_confirm, on_goto_cancel, state);
    ui2_text_input_set_buffer(state->goto_widget, state->goto_buf, sizeof(state->goto_buf));

    ui2_layout_add(root, UI2_WIDGET(state->goto_widget));

    ui2_screen_set_root(state->screen, root);
    ui2_screen_focus_set(state->screen, UI2_WIDGET(state->goto_widget));
}

void reader_view_setup_search(reader_state_t *state) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();

    null_reading_buttons(state);

    if (ui2_osk_is_active()) {
        return;
    }

    state->mode = MODE_SEARCH;
    state->search_buf[0] = '\0';

    if (!keyboard_is_available()) {
        if (ui2_osk_input_text("Search:", state->search_buf, sizeof(state->search_buf), NULL, false)) {
            return;
        }
        state->mode = MODE_READING;
        return;
    }

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);

    state->search_widget = ui2_text_input_create(0, rows - 4, cols, 4);
    ui2_text_input_set_title(state->search_widget, "Search Forward");
    ui2_text_input_set_label(state->search_widget, "Text:");
    ui2_text_input_set_hints(state->search_widget, "Type text  Enter Search", "ESC Cancel");
    ui2_text_input_set_callbacks(state->search_widget, on_search_confirm, on_search_cancel, state);
    ui2_text_input_set_buffer(state->search_widget, state->search_buf, sizeof(state->search_buf));

    ui2_layout_add(root, UI2_WIDGET(state->search_widget));

    ui2_screen_set_root(state->screen, root);
    ui2_screen_focus_set(state->screen, UI2_WIDGET(state->search_widget));
}

void reader_view_setup_receiving(reader_state_t *state) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    null_reading_buttons(state);

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);

    int btn_w = 10;
    int btn_x = (cols - btn_w) / 2;
    int btn_y = (rows - 9) / 2 + 6;

    state->btn_cancel = ui2_button_create(btn_x, btn_y, btn_w, 3, "\xE2\x9C\x98");
    ui2_button_set_callback(state->btn_cancel, on_cancel_click, state);

    ui2_layout_add(root, UI2_WIDGET(state->btn_cancel));

    ui2_screen_set_root(state->screen, root);
}

void reader_view_render_receiving(reader_state_t *state) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    text_mode_clear(TEXT_COLOR_BLACK);

    int win_w = cols - 4;
    if (win_w < 20) win_w = 20;
    if (win_w > 60) win_w = 60;
    int win_h = 9;
    int win_x = (cols - win_w) / 2;
    int win_y = (rows - win_h) / 2;
    int center_x = cols / 2;

    const char *title = state->receiving_filename[0] ? state->receiving_filename : "Receiving File";
    draw_window(win_x, win_y, win_w, win_h, title);

    if (state->receiving_filename[0]) {
        text_mode_print_at_attr(center_x - 6, win_y + 2, "Downloading...", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    } else {
        text_mode_print_at_attr(center_x - 14, win_y + 2, "Receiving file via serial...", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    }

    int bar_width = win_w - 4;
    if (bar_width > 50) bar_width = 50;
    int bar_x = (cols - bar_width) / 2;
    int bar_y = win_y + 4;

    for (int x = 0; x < bar_width; x++) {
        text_mode_print_at_color(bar_x + x, bar_y, "_", TEXT_COLOR_CYAN);
    }

    if (state->btn_cancel) {
        UI2_WIDGET(state->btn_cancel)->vtable->draw(UI2_WIDGET(state->btn_cancel));
    }

    text_mode_flush();
}

void reader_view_update_progress(const reader_state_t *state, size_t received, size_t total) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    int win_w = cols - 4;
    if (win_w < 20) win_w = 20;
    if (win_w > 60) win_w = 60;
    int bar_width = win_w - 4;
    if (bar_width > 50) bar_width = 50;
    int bar_x = (cols - bar_width) / 2;
    int bar_y = (rows - 9) / 2 + 4;

    int filled = 0;
    if (total > 0) filled = (int)((size_t)received * bar_width / total);
    if (filled > bar_width) filled = bar_width;

    for (int x = 0; x < bar_width; x++) {
        text_mode_print_at_color(bar_x + x, bar_y, x < filled ? "=" : "-", x < filled ? TEXT_COLOR_GREEN : TEXT_COLOR_CYAN);
    }

    char info[48];
    int pct = total > 0 ? (int)(received * 100 / total) : 0;
    snprintf(info, sizeof(info), "%d%%  (%d/%d KB)", pct, (int)(received / 1024), (int)(total / 1024));
    text_mode_print_at_attr(bar_x, bar_y - 1, info, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    text_mode_flush();
}
