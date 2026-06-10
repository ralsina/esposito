#include "ui2_osk.h"
#include "hardware.h"
#include "text_mode.h"
#include "os_core.h"
#include "lucide_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_INPUT_LENGTH 256
#define KEYBOARD_ROWS 4
#define KEYBOARD_COLS 10
#define MAX_BUTTONS 50

static const char *TAG = "ui2_osk";

typedef struct {
    int x, y, width, height;
    uint16_t fg, bg;
    char label[4];
} key_rect_t;

typedef struct {
    char input_buffer[MAX_INPUT_LENGTH];
    int cursor_pos;
    bool shift_active;
    bool symbol_mode;
    bool mask_input;
    bool is_active;

    key_rect_t keys[MAX_BUTTONS];
    int total_keys;
    int key_rows;
    int key_cols[5];

    int title_y;
    int input_y;
    int keyboard_start_y;
    int input_display_width;

    char *user_buffer;
    int max_len;
    ui2_osk_result_t result;

    text_mode_snapshot_t *saved_screen;
} osk_state_t;

static ui2_osk_result_t g_last_result = UI2_OSK_RESULT_CANCELLED;
static osk_state_t *g_state = NULL;

static const char *keyboard_layout[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";"},
    {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"}
};

static const char *symbol_layout[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
    {"-", "_", "=", "+", "[", "]", "{", "}", "|", "\\"},
    {":", ";", "'", "\"", "<", ">", ",", ".", "?", "/"},
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}
};

static const char *special_labels[6] = {
    ICON_X, ICON_ARROW_BIG_UP, ICON_MENU, ICON_ARROW_BIG_LEFT, " ", ICON_CHECK
};

static const char *special_keys[6] = {
    "ESC", "SHIFT", "SYMBOL", "BSP", "SPACE", "ENT"
};

static char get_shifted_char(char c);
static void finish_osk(ui2_osk_result_t result);
static void draw_input_display(void);
static void update_key_labels(void);
static void draw_all_keys(void);
static void draw_key(const key_rect_t *key);
static int key_hit_test(int col, int row);

static char get_shifted_char(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    if (c >= '0' && c <= '9') {
        const char *shifted = ")!@#$%^&*(";
        return shifted[c - '0'];
    }
    switch (c) {
        case ';': return ':';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        case '`': return '~';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case '\'': return '"';
        default: return c;
    }
}

static const char *get_key_label(int key_idx) {
    osk_state_t *s = g_state;
    if (!s) return "";

    if (key_idx < 0 || key_idx >= s->total_keys) return "";

    if (key_idx >= 40) {
        return special_labels[key_idx - 40];
    }

    int row = key_idx / 10;
    int col = key_idx % 10;

    if (s->symbol_mode) {
        return symbol_layout[row][col];
    }

    return keyboard_layout[row][col];
}

static void set_key_from_mode(int key_idx) {
    osk_state_t *s = g_state;
    if (!s || key_idx < 0 || key_idx >= s->total_keys) return;

    if (key_idx >= 40) return;

    int row = key_idx / 10;
    int col = key_idx % 10;
    const char *label;
    char c;

    if (s->symbol_mode) {
        label = symbol_layout[row][col];
        c = label[0];
        if (s->shift_active) c = get_shifted_char(c);
    } else {
        label = keyboard_layout[row][col];
        c = label[0];
        if (c >= 'a' && c <= 'z' && s->shift_active) c = c - 'a' + 'A';
        else if (c >= '0' && c <= '9' && s->shift_active) {
            const char *shifted = ")!@#$%^&*(";
            c = shifted[c - '0'];
        } else if (s->shift_active) {
            c = get_shifted_char(c);
        }
    }

    s->keys[key_idx].label[0] = c;
    s->keys[key_idx].label[1] = '\0';
}

bool ui2_osk_input_text(const char *title, char *buffer, int max_len,
                        const char *initial_text, bool mask_input) {
    if (!buffer || max_len <= 0) return false;
    if (g_state != NULL) return false;

    osk_state_t *state = (osk_state_t *)calloc(1, sizeof(osk_state_t));
    if (!state) return false;

    state->cursor_pos = 0;
    state->shift_active = false;
    state->symbol_mode = false;
    state->mask_input = mask_input;
    state->is_active = true;
    state->user_buffer = buffer;
    state->max_len = max_len;
    state->result = UI2_OSK_RESULT_CANCELLED;

    if (initial_text) {
        strncpy(state->input_buffer, initial_text, MAX_INPUT_LENGTH - 1);
        state->input_buffer[MAX_INPUT_LENGTH - 1] = '\0';
        state->cursor_pos = strlen(state->input_buffer);
    } else {
        state->input_buffer[0] = '\0';
    }

    state->saved_screen = text_mode_save_snapshot();

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    state->title_y = 0;
    state->input_y = 1;
    state->keyboard_start_y = 2;
    state->input_display_width = cols;

    int avail_rows = rows - state->keyboard_start_y;
    int total_key_rows = 5;
    int key_h = avail_rows / total_key_rows;
    if (key_h < 1) key_h = 1;

    state->total_keys = 0;
    state->key_rows = total_key_rows;
    int key_y = state->keyboard_start_y;

    for (int r = 0; r < 4; r++) {
        int key_w = cols / 10;
        if (key_w < 2) key_w = 2;
        int total_w = key_w * 10;
        int start_x = (cols - total_w) / 2;
        state->key_cols[r] = 10;
        for (int c = 0; c < 10; c++) {
            int idx = state->total_keys;
            state->keys[idx].x = start_x + c * key_w;
            state->keys[idx].y = key_y;
            state->keys[idx].width = key_w;
            state->keys[idx].height = key_h;
            state->keys[idx].fg = TEXT_COLOR_WHITE;
            state->keys[idx].bg = TEXT_COLOR_BLUE;
            state->keys[idx].label[0] = keyboard_layout[r][c][0];
            state->keys[idx].label[1] = '\0';
            state->total_keys++;
        }
        key_y += key_h;
    }

    {
        int special_count = 6;
        int key_w = cols / special_count;
        if (key_w < 3) key_w = 3;
        int total_w = key_w * special_count;
        int start_x = (cols - total_w) / 2;
        state->key_cols[4] = special_count;
        for (int c = 0; c < special_count; c++) {
            int idx = state->total_keys;
            state->keys[idx].x = start_x + c * key_w;
            state->keys[idx].y = key_y;
            state->keys[idx].width = key_w;
            state->keys[idx].height = key_h;
            state->keys[idx].label[0] = special_labels[c][0];
            state->keys[idx].label[1] = '\0';
            if (strlen(special_labels[c]) > 1) {
                state->keys[idx].label[0] = special_labels[c][0];
                state->keys[idx].label[1] = special_labels[c][1];
                state->keys[idx].label[2] = special_labels[c][2];
                state->keys[idx].label[3] = '\0';
            }
            if (strcmp(special_keys[c], "ESC") == 0) {
                state->keys[idx].fg = TEXT_COLOR_WHITE;
                state->keys[idx].bg = TEXT_COLOR_RED;
            } else if (strcmp(special_keys[c], "ENT") == 0) {
                state->keys[idx].fg = TEXT_COLOR_WHITE;
                state->keys[idx].bg = TEXT_COLOR_GREEN;
            } else if (strcmp(special_keys[c], "SPACE") == 0) {
                state->keys[idx].fg = TEXT_COLOR_WHITE;
                state->keys[idx].bg = TEXT_COLOR_BLUE;
            } else {
                state->keys[idx].fg = TEXT_COLOR_WHITE;
                state->keys[idx].bg = TEXT_COLOR_BLUE;
            }
            state->total_keys++;
        }
    }

    g_state = state;
    text_mode_clear(TEXT_COLOR_BLACK);
    draw_input_display();

    if (title) {
        text_mode_print_at_attr_bg(0, state->title_y, title, TEXT_COLOR_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);
    }

    update_key_labels();
    draw_all_keys();
    text_mode_flush();
    return true;
}

static void draw_key(const key_rect_t *key) {
    int x = key->x;
    int y = key->y;
    int w = key->width;
    int h = key->height;

    for (int row = 0; row < h; row++) {
        uint8_t attr = TEXT_ATTR_NORMAL;
        if (h > 1) {
            if (row == 0) attr |= TEXT_ATTR_BORDER_TOP;
            if (row == h - 1) attr |= TEXT_ATTR_UNDERLINE;
        }
        for (int col = 0; col < w; col++) {
            uint8_t ca = attr;
            if (col == 0) ca |= TEXT_ATTR_BORDER_LEFT;
            if (col == w - 1) ca |= TEXT_ATTR_BORDER_RIGHT;
            text_mode_print_at_attr_bg(x + col, y + row, " ", key->fg, key->bg, ca);
        }
    }

    int label_len = strlen(key->label);
    int lx = x + (w - label_len) / 2;
    int ly = y + h / 2;
    if (label_len > 0 && lx >= 0) {
        char buf[8];
        strncpy(buf, key->label, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        text_mode_print_at_attr_bg(lx, ly, buf, key->fg, key->bg, TEXT_ATTR_BOLD);
    }
}

static void draw_all_keys(void) {
    osk_state_t *s = g_state;
    if (!s) return;
    for (int i = 0; i < s->total_keys; i++) {
        draw_key(&s->keys[i]);
    }
}

static void update_key_labels(void) {
    osk_state_t *s = g_state;
    if (!s) return;

    for (int i = 0; i < 40; i++) {
        set_key_from_mode(i);
    }

    for (int i = 40; i < s->total_keys; i++) {
        int special_idx = i - 40;
        if (special_idx == 1) {
            if (s->shift_active) {
                s->keys[i].fg = TEXT_COLOR_YELLOW;
                s->keys[i].bg = TEXT_COLOR_RED;
            } else {
                s->keys[i].fg = TEXT_COLOR_WHITE;
                s->keys[i].bg = TEXT_COLOR_BLUE;
            }
        } else if (special_idx == 2) {
            if (s->symbol_mode) {
                s->keys[i].fg = TEXT_COLOR_YELLOW;
                s->keys[i].bg = TEXT_COLOR_BLUE;
            } else {
                s->keys[i].fg = TEXT_COLOR_WHITE;
                s->keys[i].bg = TEXT_COLOR_BLUE;
            }
        }
    }
}

static void draw_input_display(void) {
    osk_state_t *s = g_state;
    if (!s) return;

    int cols = s->input_display_width;

    for (int x = 0; x < cols; x++) {
        text_mode_print_at_attr_bg(x, s->input_y, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
    }

    const char *display = s->mask_input ? "****************" : s->input_buffer;
    int text_len = strlen(display);
    int max_w = cols - 2;
    int offset = 0;
    if (text_len > max_w) {
        offset = text_len - max_w;
        text_len = max_w;
    }
    int text_x = 1;

    for (int i = 0; i < text_len; i++) {
        int cx = text_x + i;
        if (cx < cols - 1) {
            char str[2] = {display[offset + i], '\0'};
            if (i == s->cursor_pos - offset) {
                text_mode_print_at_attr_bg(cx, s->input_y, str, TEXT_COLOR_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);
            } else {
                text_mode_print_at_attr_bg(cx, s->input_y, str, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
            }
        }
    }

    int cursor_display = s->cursor_pos;
    if (cursor_display < offset) cursor_display = offset;
    if (cursor_display >= offset + text_len) cursor_display = offset + text_len;
    if (cursor_display == text_len + offset && text_x + (cursor_display - offset) < cols - 1) {
        text_mode_print_at_attr_bg(text_x + (cursor_display - offset), s->input_y, "_", TEXT_COLOR_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);
    }
}

static int key_hit_test(int col, int row) {
    osk_state_t *s = g_state;
    if (!s) return -1;

    for (int i = 0; i < s->total_keys; i++) {
        if (col >= s->keys[i].x && col < s->keys[i].x + s->keys[i].width &&
            row >= s->keys[i].y && row < s->keys[i].y + s->keys[i].height) {
            return i;
        }
    }
    return -1;
}

static void handle_key_press_impl(int key_idx) {
    osk_state_t *s = g_state;
    if (!s) return;

    if (key_idx < 0 || key_idx >= s->total_keys) return;

    int special_idx = key_idx >= 40 ? key_idx - 40 : -1;

    if (special_idx == 0) {
        finish_osk(UI2_OSK_RESULT_CANCELLED);
        return;
    }
    if (special_idx == 5) {
        finish_osk(UI2_OSK_RESULT_CONFIRMED);
        return;
    }
    if (special_idx == 3) {
        if (s->cursor_pos > 0) {
            s->cursor_pos--;
            s->input_buffer[s->cursor_pos] = '\0';
            draw_input_display();
            text_mode_flush();
        }
        return;
    }
    if (special_idx == 4) {
        if (s->cursor_pos < MAX_INPUT_LENGTH - 1) {
            s->input_buffer[s->cursor_pos++] = ' ';
            s->input_buffer[s->cursor_pos] = '\0';
            draw_input_display();
            text_mode_flush();
        }
        return;
    }
    if (special_idx == 1) {
        s->shift_active = !s->shift_active;
        update_key_labels();
        draw_all_keys();
        text_mode_flush();
        return;
    }
    if (special_idx == 2) {
        s->symbol_mode = !s->symbol_mode;
        update_key_labels();
        draw_all_keys();
        text_mode_flush();
        return;
    }

    if (key_idx >= 0 && key_idx < 40) {
        char c = s->keys[key_idx].label[0];

        if (s->cursor_pos < MAX_INPUT_LENGTH - 1) {
            int cur_len = strlen(s->input_buffer);
            if (s->cursor_pos < cur_len) {
                for (int i = cur_len; i >= s->cursor_pos; i--) {
                    s->input_buffer[i + 1] = s->input_buffer[i];
                }
            }
            s->input_buffer[s->cursor_pos++] = c;
            s->input_buffer[s->cursor_pos] = '\0';
            draw_input_display();
            text_mode_flush();
        }
    }
}

bool ui2_osk_handle_event(app_context_t *ctx, event_t *event) {
    if (!g_state || !g_state->is_active) return false;

    osk_state_t *s = g_state;

    if (event->type == EVENT_TOUCH && event->touch.pressed) {
        int char_w = text_mode_get_char_width();
        int char_h = text_mode_get_char_height();
        if (char_w <= 0) char_w = 8;
        if (char_h <= 0) char_h = 16;
        int col = event->touch.x / char_w;
        int row = event->touch.y / char_h;

        if (row >= s->input_y && row < s->input_y + 1) {
            if (col >= 1 && col < s->input_display_width - 1) {
                const char *display = s->mask_input ? "****************" : s->input_buffer;
                int text_len = strlen(display);
                int max_w = s->input_display_width - 2;
                int text_x = 1 + (max_w - text_len) / 2;
                int new_pos = col - text_x;
                if (new_pos < 0) new_pos = 0;
                if (new_pos > text_len) new_pos = text_len;
                s->cursor_pos = new_pos;
                draw_input_display();
                text_mode_flush();
                return true;
            }
        }

        int key_idx = key_hit_test(col, row);
        if (key_idx >= 0) {
            handle_key_press_impl(key_idx);
            return true;
        }
        return false;
    }

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        if (key == 27) {
            finish_osk(UI2_OSK_RESULT_CANCELLED);
            return true;
        }
        if (key == '\n' || key == '\r') {
            finish_osk(UI2_OSK_RESULT_CONFIRMED);
            return true;
        }
        if (key == '\b' || key == 127) {
            if (s->cursor_pos > 0) {
                s->cursor_pos--;
                s->input_buffer[s->cursor_pos] = '\0';
                draw_input_display();
                text_mode_flush();
            }
            return true;
        }
        if (key == 0x11 || key == 0x12) {
            s->shift_active = !s->shift_active;
            update_key_labels();
            draw_all_keys();
            text_mode_flush();
            return true;
        }
        if (key >= 32 && key <= 126) {
            char c = key;
            if (s->shift_active) c = get_shifted_char(c);

            if (s->cursor_pos < MAX_INPUT_LENGTH - 1) {
                int cur_len = strlen(s->input_buffer);
                if (s->cursor_pos < cur_len) {
                    for (int i = cur_len; i >= s->cursor_pos; i--) {
                        s->input_buffer[i + 1] = s->input_buffer[i];
                    }
                }
                s->input_buffer[s->cursor_pos++] = c;
                if (strlen(s->input_buffer) >= MAX_INPUT_LENGTH - 1) {
                    s->input_buffer[MAX_INPUT_LENGTH - 1] = '\0';
                }
                draw_input_display();
                text_mode_flush();
            }
            return true;
        }
    }

    return false;
}

bool ui2_osk_is_active(void) {
    return g_state != NULL && g_state->is_active;
}

ui2_osk_result_t ui2_osk_get_result(void) {
    return g_last_result;
}

static void finish_osk(ui2_osk_result_t result) {
    if (!g_state) return;

    osk_state_t *s = g_state;
    s->is_active = false;
    s->result = result;
    g_last_result = result;

    if (result == UI2_OSK_RESULT_CONFIRMED && s->user_buffer) {
        strncpy(s->user_buffer, s->input_buffer, s->max_len - 1);
        s->user_buffer[s->max_len - 1] = '\0';
    }

    if (s->saved_screen) {
        text_mode_restore_snapshot(s->saved_screen);
        text_mode_free_snapshot(s->saved_screen);
        s->saved_screen = NULL;
    } else {
        text_mode_clear(TEXT_COLOR_BLACK);
    }

    free(s);
    g_state = NULL;
}
