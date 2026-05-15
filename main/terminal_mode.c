#include "terminal_mode.h"

#include "hardware.h"
#include "terminal_vt100.h"
#include "text_mode.h"

#include "sd_card.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

/* Map ANSI color index 0-7 to CGA text_mode color index 0-15.
 * Terminal palette uses full-brightness RGB565 values that match the
 * CGA bright colors (indices 8-15), except black (0 stays 0). */
static const uint8_t ansi_to_cga[8] = {
    TEXT_COLOR_BLACK,           /* 0 = black        */
    TEXT_COLOR_BRIGHT_RED,      /* 1 = red          */
    TEXT_COLOR_BRIGHT_GREEN,    /* 2 = green        */
    TEXT_COLOR_BRIGHT_YELLOW,   /* 3 = yellow       */
    TEXT_COLOR_BRIGHT_BLUE,     /* 4 = blue         */
    TEXT_COLOR_BRIGHT_MAGENTA,  /* 5 = magenta      */
    TEXT_COLOR_BRIGHT_CYAN,     /* 6 = cyan         */
    TEXT_COLOR_BRIGHT_WHITE,    /* 7 = white        */
};

#define TERM_CELL_W 5
#define TERM_CELL_H 8
#define TERM_STATUS_ROWS 1
#define TERM_MAX_STATUS 80

typedef struct {
    uint16_t palette[8];
    char last_screen[TERM_MAX_BUFFER_SIZE];
    vt100_attr_t last_attrs[TERM_MAX_BUFFER_SIZE];
    int last_screen_reverse;
    int cursor_blink_state;
    int prev_cursor_x;
    int prev_cursor_y;
    int blink_counter;
    char status[TERM_MAX_STATUS];
    char status_last[TERM_MAX_STATUS];
    int status_dirty;
    char title[48];
    terminal_mode_write_cb write_cb;
    terminal_mode_title_cb title_cb;
    int initialized;
    vt100_t vt;
} terminal_mode_impl_t;

struct terminal_mode {
    terminal_mode_impl_t impl;
};

static terminal_mode_t g_terminal;
static terminal_mode_t *g_active = NULL;

static void terminal_write_to_backend(const char *data, size_t len) {
    if (g_active && g_active->impl.write_cb) {
        g_active->impl.write_cb(data, len);
    }
}

static void terminal_title_from_vt(const char *title) {
    if (!g_active) {
        return;
    }

    strncpy(g_active->impl.title, title, sizeof(g_active->impl.title) - 1);
    g_active->impl.title[sizeof(g_active->impl.title) - 1] = '\0';
    g_active->impl.status_dirty = 1;

    if (g_active->impl.title_cb) {
        g_active->impl.title_cb(title);
    }
}

static void draw_graphics_char(int px, int py, char c, uint16_t fg) {
    int mid_x = px + TERM_CELL_W / 2;
    int mid_y = py + TERM_CELL_H / 2;
    int right = px + TERM_CELL_W - 1;
    int bottom = py + TERM_CELL_H - 1;

    switch (c) {
        case 'q':
            display_fill_rect(px, mid_y, TERM_CELL_W, 1, fg);
            break;
        case 'x':
            display_fill_rect(mid_x, py, 1, TERM_CELL_H, fg);
            break;
        case 'l':
            display_fill_rect(mid_x, mid_y, right - mid_x + 1, 1, fg);
            display_fill_rect(mid_x, mid_y, 1, bottom - mid_y + 1, fg);
            break;
        case 'k':
            display_fill_rect(px, mid_y, mid_x - px + 1, 1, fg);
            display_fill_rect(mid_x, mid_y, 1, bottom - mid_y + 1, fg);
            break;
        case 'm':
            display_fill_rect(mid_x, mid_y, right - mid_x + 1, 1, fg);
            display_fill_rect(mid_x, py, 1, mid_y - py + 1, fg);
            break;
        case 'j':
            display_fill_rect(px, mid_y, mid_x - px + 1, 1, fg);
            display_fill_rect(mid_x, py, 1, mid_y - py + 1, fg);
            break;
        case 'n':
            display_fill_rect(px, mid_y, TERM_CELL_W, 1, fg);
            display_fill_rect(mid_x, py, 1, TERM_CELL_H, fg);
            break;
        case 't':
            display_fill_rect(mid_x, mid_y, right - mid_x + 1, 1, fg);
            display_fill_rect(mid_x, py, 1, TERM_CELL_H, fg);
            break;
        case 'u':
            display_fill_rect(px, mid_y, mid_x - px + 1, 1, fg);
            display_fill_rect(mid_x, py, 1, TERM_CELL_H, fg);
            break;
        case 'v':
            display_fill_rect(px, mid_y, TERM_CELL_W, 1, fg);
            display_fill_rect(mid_x, py, 1, mid_y - py + 1, fg);
            break;
        case 'w':
            display_fill_rect(px, mid_y, TERM_CELL_W, 1, fg);
            display_fill_rect(mid_x, mid_y, 1, bottom - mid_y + 1, fg);
            break;
        default:
            break;
    }
}

static void draw_cell(terminal_mode_impl_t *impl, int x, int y, char c, vt100_attr_t attr) {
    uint8_t fg_cga = ansi_to_cga[attr.fg & 7];
    uint8_t bg_cga = ansi_to_cga[attr.bg & 7];
    int reverse = attr.reverse ^ impl->vt.screen_reverse;
    if (reverse) {
        uint8_t tmp = fg_cga;
        fg_cga = bg_cga;
        bg_cga = tmp;
    }

    uint8_t tm_attr = TEXT_ATTR_NORMAL;
    if (attr.bold)      tm_attr |= TEXT_ATTR_BOLD;
    if (attr.underline) tm_attr |= TEXT_ATTR_UNDERLINE;
    if (attr.graphics)  tm_attr |= TEXT_ATTR_LINE_DRAWING;

    char buf[2] = {c, '\0'};
    text_mode_print_at_attr_bg(x, y, buf, fg_cga, bg_cga, tm_attr);
}

static void draw_cursor(terminal_mode_impl_t *impl) {
    int cx = impl->vt.cursor_x;
    int cy = impl->vt.cursor_y;

    if (cx < 0 || cx >= impl->vt.cols || cy < 0 || cy >= impl->vt.rows) {
        return;
    }

    if (impl->prev_cursor_x >= 0 && impl->prev_cursor_y >= 0 &&
        (impl->prev_cursor_x != cx || impl->prev_cursor_y != cy)) {
        char prev_char = vt100_get_char(&impl->vt, impl->prev_cursor_x, impl->prev_cursor_y);
        vt100_attr_t prev_attr = vt100_get_attr(&impl->vt, impl->prev_cursor_x, impl->prev_cursor_y);
        draw_cell(impl, impl->prev_cursor_x, impl->prev_cursor_y, prev_char, prev_attr);
    }

    int px = cx * TERM_CELL_W;
    int py = cy * TERM_CELL_H;
    char ch = vt100_get_char(&impl->vt, cx, cy);
    vt100_attr_t attr = vt100_get_attr(&impl->vt, cx, cy);

    uint16_t fg = impl->palette[attr.fg & 7];
    uint16_t bg = impl->palette[attr.bg & 7];
    int reverse = attr.reverse ^ impl->vt.screen_reverse;
    if (reverse) {
        uint16_t tmp = fg;
        fg = bg;
        bg = tmp;
    }

    if (impl->cursor_blink_state && impl->vt.cursor_visible) {
        /* Draw cursor block directly on display — transient, not stored in grid */
        display_fill_rect(px, py, TERM_CELL_W, TERM_CELL_H, fg);
        if (ch != ' ') {
            if (attr.graphics) {
                draw_graphics_char(px, py, ch, bg);
            } else {
                display_draw_char_at(px, py, ch, bg, fg);
            }
        }
    } else {
        draw_cell(impl, cx, cy, ch, attr);
    }

    impl->prev_cursor_x = cx;
    impl->prev_cursor_y = cy;
}

static void draw_status_bar(terminal_mode_impl_t *impl) {
    char composed[TERM_MAX_STATUS];
    if (impl->status[0]) {
        snprintf(composed, sizeof(composed), "%s", impl->status);
    } else {
        snprintf(composed, sizeof(composed), "%s", impl->title[0] ? impl->title : "Terminal Mode");
    }

    if (!impl->status_dirty && strcmp(composed, impl->status_last) == 0) {
        return;
    }

    strncpy(impl->status_last, composed, sizeof(impl->status_last) - 1);
    impl->status_last[sizeof(impl->status_last) - 1] = '\0';
    impl->status_dirty = 0;

    int row = impl->vt.rows;
    int cols = impl->vt.cols;

    char padded[TERM_MAX_COLS + 1];
    if (cols > TERM_MAX_COLS) {
        cols = TERM_MAX_COLS;
    }
    memset(padded, ' ', cols);
    padded[cols] = '\0';

    size_t len = strlen(composed);
    if ((int)len > cols) {
        len = (size_t)cols;
    }
    memcpy(padded, composed, len);

    text_mode_print_at_attr_bg(0, row, padded, TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BRIGHT_BLUE, TEXT_ATTR_NORMAL);
}

static void send_bytes(terminal_mode_impl_t *impl, const char *data, size_t len) {
    if (impl->write_cb) {
        impl->write_cb(data, len);
    }
}

terminal_mode_t *terminal_mode_default(void) {
    return &g_terminal;
}

bool terminal_mode_init(terminal_mode_t *term, int cols, int rows, terminal_mode_write_cb write_cb) {
    if (!term) {
        return false;
    }

    terminal_mode_impl_t *impl = &term->impl;
    memset(impl, 0, sizeof(*impl));

    impl->palette[0] = 0x0000;
    impl->palette[1] = 0xF800;
    impl->palette[2] = 0x07E0;
    impl->palette[3] = 0xFFE0;
    impl->palette[4] = 0x001F;
    impl->palette[5] = 0xF81F;
    impl->palette[6] = 0x07FF;
    impl->palette[7] = 0xFFFF;

    impl->write_cb = write_cb;
    impl->cursor_blink_state = 1;
    impl->prev_cursor_x = -1;
    impl->prev_cursor_y = -1;
    impl->status_dirty = 1;
    strncpy(impl->title, "Terminal Mode", sizeof(impl->title) - 1);

    g_active = term;
    vt100_init(&impl->vt, terminal_write_to_backend);
    vt100_set_title_callback(&impl->vt, terminal_title_from_vt);

    if (cols <= 0) cols = 64;
    if (rows <= 0) rows = 29;
    vt100_set_geometry(&impl->vt, cols, rows);

    memset(impl->last_screen, 0, sizeof(impl->last_screen));
    memset(impl->last_attrs, 0, sizeof(impl->last_attrs));

    text_mode_clear(TEXT_COLOR_BLACK);
    impl->initialized = 1;
    return true;
}

void terminal_mode_reset(terminal_mode_t *term) {
    if (!term) return;
    terminal_mode_impl_t *impl = &term->impl;
    vt100_clear_screen(&impl->vt);
    memset(impl->last_screen, 0, sizeof(impl->last_screen));
    memset(impl->last_attrs, 0, sizeof(impl->last_attrs));
    impl->prev_cursor_x = -1;
    impl->prev_cursor_y = -1;
    impl->status_dirty = 1;
}

void terminal_mode_set_write_callback(terminal_mode_t *term, terminal_mode_write_cb cb) {
    if (!term) return;
    term->impl.write_cb = cb;
}

void terminal_mode_set_title_callback(terminal_mode_t *term, terminal_mode_title_cb cb) {
    if (!term) return;
    term->impl.title_cb = cb;
}

void terminal_mode_process_bytes(terminal_mode_t *term, const char *data, size_t len) {
    if (!term || !data) return;
    terminal_mode_impl_t *impl = &term->impl;
    for (size_t i = 0; i < len; i++) {
        vt100_process(&impl->vt, data[i]);
    }
}

void terminal_mode_handle_key(terminal_mode_t *term, char key, uint8_t modifiers) {
    if (!term) return;
    terminal_mode_impl_t *impl = &term->impl;

    int ctrl = (modifiers & MODIFIER_CTRL) != 0;
    int alt = (modifiers & MODIFIER_ALT) != 0;
    int fn = (modifiers & MODIFIER_FN) != 0;
    int fn2 = (modifiers & MODIFIER_FN2) != 0;

    if (fn2) {
        const char *fkeys[] = {"\033OP", "\033OQ", "\033OR", "\033OS"};
        int idx = -1;
        if (key == 'q' || key == 'Q') idx = 0;
        else if (key == 'w' || key == 'W') idx = 1;
        else if (key == 'a' || key == 'A') idx = 2;
        else if (key == 's' || key == 'S') idx = 3;
        if (idx >= 0) {
            send_bytes(impl, fkeys[idx], strlen(fkeys[idx]));
            return;
        }
    }

    if (fn) {
        const char *up = impl->vt.app_cursor_keys ? "\033OA" : "\033[A";
        const char *down = impl->vt.app_cursor_keys ? "\033OB" : "\033[B";
        const char *left = impl->vt.app_cursor_keys ? "\033OD" : "\033[D";
        const char *right = impl->vt.app_cursor_keys ? "\033OC" : "\033[C";
        const char *arrow = NULL;

        if (key == 'w' || key == 'W') arrow = up;
        else if (key == 's' || key == 'S') arrow = down;
        else if (key == 'a' || key == 'A') arrow = left;
        else if (key == 'd' || key == 'D') arrow = right;

        if (arrow) {
            send_bytes(impl, arrow, strlen(arrow));
            return;
        }
    }

    if (ctrl && key >= 32 && key <= 126) {
        char ctrl_ch = (char)(key & 0x1F);
        send_bytes(impl, &ctrl_ch, 1);
        return;
    }

    if (alt) {
        send_bytes(impl, "\033", 1);
        send_bytes(impl, &key, 1);
        return;
    }

    switch (key) {
        case '\n':
        case '\r':
            if (impl->vt.line_feed_mode) {
                send_bytes(impl, "\r\n", 2);
            } else {
                send_bytes(impl, "\r", 1);
            }
            return;
        case '\b':
            send_bytes(impl, "\b", 1);
            return;
        case '\t':
            send_bytes(impl, "\t", 1);
            return;
        case 27:
        case 5:
            send_bytes(impl, "\033", 1);
            return;
        default:
            break;
    }

    if (key >= 32 && key <= 126) {
        send_bytes(impl, &key, 1);
    }
}

void terminal_mode_set_status(terminal_mode_t *term, const char *status) {
    if (!term) return;
    terminal_mode_impl_t *impl = &term->impl;

    if (!status) {
        impl->status[0] = '\0';
    } else {
        strncpy(impl->status, status, sizeof(impl->status) - 1);
        impl->status[sizeof(impl->status) - 1] = '\0';
    }
    impl->status_dirty = 1;
}

void terminal_mode_render(terminal_mode_t *term) {
    if (!term || !term->impl.initialized) {
        return;
    }

    terminal_mode_impl_t *impl = &term->impl;

    impl->blink_counter++;
    if (impl->blink_counter >= 10) {
        impl->blink_counter = 0;
        impl->cursor_blink_state = !impl->cursor_blink_state;
    }

    if (impl->vt.screen_reverse != impl->last_screen_reverse) {
        impl->last_screen_reverse = impl->vt.screen_reverse;
        memset(impl->last_screen, 0, sizeof(impl->last_screen));
        memset(impl->last_attrs, 0, sizeof(impl->last_attrs));
        impl->prev_cursor_x = -1;
        impl->prev_cursor_y = -1;
    }

    for (int y = 0; y < impl->vt.rows; y++) {
        for (int x = 0; x < impl->vt.cols; x++) {
            int idx = y * impl->vt.cols + x;
            char c = vt100_get_char(&impl->vt, x, y);
            vt100_attr_t attr = vt100_get_attr(&impl->vt, x, y);

            if (c != impl->last_screen[idx] ||
                attr.fg != impl->last_attrs[idx].fg ||
                attr.bg != impl->last_attrs[idx].bg ||
                attr.bold != impl->last_attrs[idx].bold ||
                attr.underline != impl->last_attrs[idx].underline ||
                attr.reverse != impl->last_attrs[idx].reverse ||
                attr.graphics != impl->last_attrs[idx].graphics) {

                impl->last_screen[idx] = c;
                impl->last_attrs[idx] = attr;
                draw_cell(impl, x, y, c, attr);
            }
        }
    }

    draw_cursor(impl);
    draw_status_bar(impl);
    impl->vt.needs_redraw = 0;
}

bool terminal_mode_save_screenshot(terminal_mode_t *term) {
    if (!term || !term->impl.initialized) return false;
    /* Grid is maintained in sync with the display via text_mode, so
     * text_mode_save_screenshot() captures the current terminal content. */
    terminal_mode_render(term);
    return text_mode_save_screenshot();
}

int terminal_mode_cols(const terminal_mode_t *term) {
    if (!term) return 0;
    return term->impl.vt.cols;
}

int terminal_mode_rows(const terminal_mode_t *term) {
    if (!term) return 0;
    return term->impl.vt.rows;
}

/* Keyboard normalization: Fn+WASD → VT100 arrow sequences, Fn+Q → Tab */
const char *terminal_mode_normalize_key(const event_t *event, size_t *out_len) {
    static char buf[4];
    
    if (!event || event->type != EVENT_KEYBOARD || !out_len) {
        if (out_len) *out_len = 0;
        return "";
    }
    
    char key = event->keyboard.key;
    uint8_t mods = event->keyboard.modifiers;
    
    /* Check for Fn+WASD arrow key mapping */
    if (mods & (MODIFIER_FN | MODIFIER_FN2)) {
        if (key == 'w' || key == 'W') {
            memcpy(buf, "\x1b[A", 3);
            *out_len = 3;
            return buf;
        } else if (key == 's' || key == 'S') {
            memcpy(buf, "\x1b[B", 3);
            *out_len = 3;
            return buf;
        } else if (key == 'a' || key == 'A') {
            memcpy(buf, "\x1b[D", 3);
            *out_len = 3;
            return buf;
        } else if (key == 'd' || key == 'D') {
            memcpy(buf, "\x1b[C", 3);
            *out_len = 3;
            return buf;
        } else if (key == 'q' || key == 'Q') {
            buf[0] = '\t';  /* Tab */
            *out_len = 1;
            return buf;
        }
    }
    
    /* Regular key pass-through */
    buf[0] = key;
    *out_len = 1;
    return buf;
}
