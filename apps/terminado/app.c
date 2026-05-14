#include "os_core.h"
#include "hardware.h"
#include "app_config.h"
#include "vt100.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "terminado";

// Terminal dimensions (matches text mode 5x8 font on 320x240 display)
#define TERM_COLS 64
#define TERM_ROWS 29
#define CELL_W 5
#define CELL_H 8
#define STATUS_ROW TERM_ROWS

// ANSI color palette (RGB565)
static const uint16_t ansi_colors[8] = {
    0x0000,  // Black
    0xF800,  // Red
    0x07E0,  // Green
    0xFFE0,  // Yellow
    0x001F,  // Blue
    0xF81F,  // Magenta
    0x07FF,  // Cyan
    0xFFFF,  // White
};

// VT100 terminal state
static vt100_t vt;
static char last_screen[TERM_MAX_BUFFER_SIZE];
static vt100_attr_t last_attrs[TERM_MAX_BUFFER_SIZE];
static int last_screen_reverse = 0;

// Cursor blink
static int cursor_blink_state = 0;
static int prev_cursor_x = -1;
static int prev_cursor_y = -1;
static char terminal_title[48] = "Terminado";
static char last_status_line[TERM_COLS + 1] = {0};
static int status_dirty = 1;

typedef struct {
    int baud;
    int data_bits;
    char parity;
    int stop_bits;
} terminal_config_t;

static terminal_config_t term_cfg;
static terminal_config_t menu_cfg;
static int menu_active = 0;
static int menu_selected = 0;

static const int baud_rates[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400};
static const int baud_rates_count = (int)(sizeof(baud_rates) / sizeof(baud_rates[0]));

static void render_terminal(void);
static uint16_t brighten_rgb565(uint16_t color);

#define MENU_ITEMS 4
#define MENU_X_COL 8
#define MENU_Y_ROW 6
#define MENU_W_COL 48
#define MENU_H_ROW 16

static int valid_data_bits(int bits) {
    return bits >= 5 && bits <= 8;
}

static int valid_stop_bits(int bits) {
    return bits == 1 || bits == 2;
}

static int valid_parity(char parity) {
    return parity == 'N' || parity == 'E' || parity == 'O';
}

static void load_terminal_config(void) {
    term_cfg.baud = config_get_int("serial/baud", 19200);
    term_cfg.data_bits = config_get_int("serial/data_bits", 8);
    term_cfg.parity = (char)config_get_int("serial/parity", 'N');
    term_cfg.stop_bits = config_get_int("serial/stop_bits", 1);

    int baud_ok = 0;
    for (int index = 0; index < baud_rates_count; index++) {
        if (baud_rates[index] == term_cfg.baud) {
            baud_ok = 1;
            break;
        }
    }

    if (!baud_ok) {
        term_cfg.baud = 19200;
    }
    if (!valid_data_bits(term_cfg.data_bits)) {
        term_cfg.data_bits = 8;
    }
    if (!valid_parity(term_cfg.parity)) {
        term_cfg.parity = 'N';
    }
    if (!valid_stop_bits(term_cfg.stop_bits)) {
        term_cfg.stop_bits = 1;
    }
}

static void save_terminal_config(void) {
    config_set_int("serial/baud", term_cfg.baud);
    config_set_int("serial/data_bits", term_cfg.data_bits);
    config_set_int("serial/parity", term_cfg.parity);
    config_set_int("serial/stop_bits", term_cfg.stop_bits);
}

static void apply_serial_config(const terminal_config_t *config) {
    serial_deinit();
    if (!serial_init(config->baud, config->data_bits, config->parity, config->stop_bits)) {
        // Keep a safe fallback if selected settings fail.
        serial_init(19200, 8, 'N', 1);
    }
}

static int baud_index_for_value(int baud) {
    for (int index = 0; index < baud_rates_count; index++) {
        if (baud_rates[index] == baud) {
            return index;
        }
    }
    return 5;  // 19200
}

static void draw_cell_text(int col, int row, const char *text, uint16_t fg, uint16_t bg) {
    int x = col * CELL_W;
    int y = row * CELL_H;
    for (int index = 0; text[index]; index++) {
        display_draw_char_at(x + index * CELL_W, y, text[index], fg, bg);
    }
}

static void draw_vt100_graphics_char(int px, int py, char c, uint16_t fg) {
    int mid_x = px + CELL_W / 2;
    int mid_y = py + CELL_H / 2;
    int right = px + CELL_W - 1;
    int bottom = py + CELL_H - 1;

    switch (c) {
        case 'q':
            display_fill_rect(px, mid_y, CELL_W, 1, fg);
            break;
        case 'x':
            display_fill_rect(mid_x, py, 1, CELL_H, fg);
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
            display_fill_rect(px, mid_y, CELL_W, 1, fg);
            display_fill_rect(mid_x, py, 1, CELL_H, fg);
            break;
        case 't':
            display_fill_rect(mid_x, mid_y, right - mid_x + 1, 1, fg);
            display_fill_rect(mid_x, py, 1, CELL_H, fg);
            break;
        case 'u':
            display_fill_rect(px, mid_y, mid_x - px + 1, 1, fg);
            display_fill_rect(mid_x, py, 1, CELL_H, fg);
            break;
        case 'v':
            display_fill_rect(px, mid_y, CELL_W, 1, fg);
            display_fill_rect(mid_x, py, 1, mid_y - py + 1, fg);
            break;
        case 'w':
            display_fill_rect(px, mid_y, CELL_W, 1, fg);
            display_fill_rect(mid_x, mid_y, 1, bottom - mid_y + 1, fg);
            break;
        default:
            break;
    }
}

static void draw_terminal_cell(int x, int y, char c, vt100_attr_t attr) {
    int px = x * CELL_W;
    int py = y * CELL_H;

    uint16_t fg = ansi_colors[attr.fg & 7];
    uint16_t bg = ansi_colors[attr.bg & 7];
    int rev = attr.reverse ^ vt.screen_reverse;
    if (rev) {
        uint16_t temp = fg;
        fg = bg;
        bg = temp;
    }

    display_fill_rect(px, py, CELL_W, CELL_H, bg);
    if (c != ' ') {
        if (attr.bold) {
            fg = brighten_rgb565(fg);
        }

        if (attr.graphics) {
            draw_vt100_graphics_char(px, py, c, fg);
        } else {
            display_draw_char_at(px, py, c, fg, bg);
        }

        if (attr.underline) {
            display_fill_rect(px, py + CELL_H - 1, CELL_W, 1, fg);
        }
    }
}

static void draw_status_bar(void) {
    char status[96];
    snprintf(status, sizeof(status), "%s | %d %d%c%d | Fn2+ESC cfg",
             terminal_title,
             term_cfg.baud,
             term_cfg.data_bits,
             term_cfg.parity,
             term_cfg.stop_bits);

    char padded[TERM_COLS + 1];
    memset(padded, ' ', TERM_COLS);
    padded[TERM_COLS] = '\0';

    size_t status_len = strlen(status);
    if (status_len > TERM_COLS) {
        status_len = TERM_COLS;
    }
    memcpy(padded, status, status_len);

    if (!status_dirty && strcmp(padded, last_status_line) == 0) {
        return;
    }
    strcpy(last_status_line, padded);
    status_dirty = 0;

    int py = STATUS_ROW * CELL_H;
    display_fill_rect(0, py, TERM_COLS * CELL_W, CELL_H, ansi_colors[4]);
    for (int index = 0; index < TERM_COLS; index++) {
        display_draw_char_at(index * CELL_W, py, padded[index], ansi_colors[7], ansi_colors[4]);
    }
}

static void draw_config_menu(void) {
    int x = MENU_X_COL * CELL_W;
    int y = MENU_Y_ROW * CELL_H;
    int w = MENU_W_COL * CELL_W;
    int h = MENU_H_ROW * CELL_H;
    int baud_idx = baud_index_for_value(menu_cfg.baud);

    display_fill_rect(x, y, w, h, ansi_colors[4]);
    display_fill_rect(x + 2, y + 2, w - 4, h - 4, ansi_colors[0]);

    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 1, "Terminado Settings", ansi_colors[6], ansi_colors[0]);
    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 3, "Shortcut: Fn2+ESC", ansi_colors[7], ansi_colors[0]);

    char line[64];
    snprintf(line, sizeof(line), "%c Baud: %d", menu_selected == 0 ? '>' : ' ', baud_rates[baud_idx]);
    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 5, line, ansi_colors[7], ansi_colors[0]);

    snprintf(line, sizeof(line), "%c Data Bits: %d", menu_selected == 1 ? '>' : ' ', menu_cfg.data_bits);
    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 6, line, ansi_colors[7], ansi_colors[0]);

    snprintf(line, sizeof(line), "%c Parity: %c", menu_selected == 2 ? '>' : ' ', menu_cfg.parity);
    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 7, line, ansi_colors[7], ansi_colors[0]);

    snprintf(line, sizeof(line), "%c Stop Bits: %d", menu_selected == 3 ? '>' : ' ', menu_cfg.stop_bits);
    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 8, line, ansi_colors[7], ansi_colors[0]);

    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 11, "W/S Select  A/D Change", ansi_colors[6], ansi_colors[0]);
    draw_cell_text(MENU_X_COL + 2, MENU_Y_ROW + 12, "Enter Apply  ESC Cancel", ansi_colors[6], ansi_colors[0]);
}

static void open_config_menu(void) {
    menu_cfg = term_cfg;
    menu_selected = 0;
    menu_active = 1;
    draw_config_menu();
}

static void close_config_menu(void) {
    menu_active = 0;
    memset(last_screen, 0, sizeof(last_screen));
    memset(last_attrs, 0, sizeof(last_attrs));
    prev_cursor_x = -1;
    prev_cursor_y = -1;
    status_dirty = 1;
    vt.needs_redraw = 1;
    render_terminal();
}

static void cycle_current_setting(int step) {
    if (menu_selected == 0) {
        int idx = baud_index_for_value(menu_cfg.baud);
        idx = (idx + step + baud_rates_count) % baud_rates_count;
        menu_cfg.baud = baud_rates[idx];
    } else if (menu_selected == 1) {
        int next = menu_cfg.data_bits + step;
        if (next < 5) next = 8;
        if (next > 8) next = 5;
        menu_cfg.data_bits = next;
    } else if (menu_selected == 2) {
        if (menu_cfg.parity == 'N') menu_cfg.parity = (step > 0) ? 'E' : 'O';
        else if (menu_cfg.parity == 'E') menu_cfg.parity = (step > 0) ? 'O' : 'N';
        else menu_cfg.parity = (step > 0) ? 'N' : 'E';
    } else if (menu_selected == 3) {
        menu_cfg.stop_bits = (menu_cfg.stop_bits == 1) ? 2 : 1;
    }
}

static void handle_config_menu_key(char key) {
    if (key == 27 || key == 5) {
        close_config_menu();
        return;
    }

    if (key == '\n' || key == '\r') {
        term_cfg = menu_cfg;
        apply_serial_config(&term_cfg);
        save_terminal_config();
        status_dirty = 1;
        close_config_menu();
        return;
    }

    if (key == 'w' || key == 'W') {
        menu_selected = (menu_selected + MENU_ITEMS - 1) % MENU_ITEMS;
        draw_config_menu();
        return;
    }
    if (key == 's' || key == 'S') {
        menu_selected = (menu_selected + 1) % MENU_ITEMS;
        draw_config_menu();
        return;
    }
    if (key == 'a' || key == 'A') {
        cycle_current_setting(-1);
        draw_config_menu();
        return;
    }
    if (key == 'd' || key == 'D') {
        cycle_current_setting(1);
        draw_config_menu();
        return;
    }
}

// Write callback: send data to serial host
static void write_to_serial(const char *data, size_t len) {
    serial_write(data, len);
}

// Title callback
static void on_title_change(const char *title) {
    os_log(TAG, "Title: %s", title);
    strncpy(terminal_title, title, sizeof(terminal_title) - 1);
    terminal_title[sizeof(terminal_title) - 1] = '\0';
    status_dirty = 1;
}

// Brighten an RGB565 color for bold rendering
static uint16_t brighten_rgb565(uint16_t color) {
    unsigned int r = (color >> 11) & 0x1F;
    unsigned int g = (color >> 5) & 0x3F;
    unsigned int b = color & 0x1F;
    r = (r + 16) > 31 ? 31 : (r + 16);
    g = (g + 32) > 63 ? 63 : (g + 32);
    b = (b + 16) > 31 ? 31 : (b + 16);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Draw cursor at current position
static void render_cursor(void) {
    int cx = vt.cursor_x;
    int cy = vt.cursor_y;
    if (cx < 0 || cx >= vt.cols || cy < 0 || cy >= vt.rows) return;

    if (prev_cursor_x >= 0 && prev_cursor_y >= 0 &&
        (prev_cursor_x != cx || prev_cursor_y != cy)) {
        char prev_ch = vt100_get_char(&vt, prev_cursor_x, prev_cursor_y);
        vt100_attr_t prev_attr = vt100_get_attr(&vt, prev_cursor_x, prev_cursor_y);
        draw_terminal_cell(prev_cursor_x, prev_cursor_y, prev_ch, prev_attr);
    }

    int px = cx * CELL_W;
    int py = cy * CELL_H;
    char ch = vt100_get_char(&vt, cx, cy);
    vt100_attr_t attr = vt100_get_attr(&vt, cx, cy);

    uint16_t fg = ansi_colors[attr.fg & 7];
    uint16_t bg = ansi_colors[attr.bg & 7];
    int rev = attr.reverse ^ vt.screen_reverse;
    if (rev) { uint16_t t = fg; fg = bg; bg = t; }

    if (cursor_blink_state && vt.cursor_visible) {
        // Draw inverted cursor block
        display_fill_rect(px, py, CELL_W, CELL_H, fg);
        if (ch != ' ') {
            if (attr.graphics) {
                draw_vt100_graphics_char(px, py, ch, bg);
            } else {
                display_draw_char_at(px, py, ch, bg, fg);
            }
        }
    } else {
        draw_terminal_cell(cx, cy, ch, attr);
    }

    prev_cursor_x = cx;
    prev_cursor_y = cy;
}

// Render changed VT100 cells to display
static void render_terminal(void) {
    // If screen reverse mode changed, force full repaint
    if (vt.screen_reverse != last_screen_reverse) {
        last_screen_reverse = vt.screen_reverse;
        memset(last_screen, 0, sizeof(last_screen));
        memset(last_attrs, 0, sizeof(last_attrs));
        prev_cursor_x = -1;
        prev_cursor_y = -1;
    }

    for (int y = 0; y < vt.rows; y++) {
        for (int x = 0; x < vt.cols; x++) {
            int idx = y * vt.cols + x;
            char c = vt100_get_char(&vt, x, y);
            vt100_attr_t attr = vt100_get_attr(&vt, x, y);

            if (c != last_screen[idx] ||
                attr.fg != last_attrs[idx].fg ||
                attr.bg != last_attrs[idx].bg ||
                attr.bold != last_attrs[idx].bold ||
                attr.underline != last_attrs[idx].underline ||
                attr.reverse != last_attrs[idx].reverse ||
                attr.graphics != last_attrs[idx].graphics) {

                last_screen[idx] = c;
                last_attrs[idx] = attr;

                draw_terminal_cell(x, y, c, attr);
            }
        }
    }

    render_cursor();
    draw_status_bar();
    vt.needs_redraw = 0;
}

// Send a key event to the host via serial
static void send_key(char key, int ctrl, int alt, int fn, int fn2) {
    // Fn2 + QWASD = F1-F4 (function keys)
    if (fn2) {
        const char *fkeys[] = {"\033OP", "\033OQ", "\033OR", "\033OS"};
        int idx = -1;
        if (key == 'q' || key == 'Q') idx = 0;
        else if (key == 'w' || key == 'W') idx = 1;
        else if (key == 'a' || key == 'A') idx = 2;
        else if (key == 's' || key == 'S') idx = 3;
        if (idx >= 0) {
            serial_write(fkeys[idx], strlen(fkeys[idx]));
            return;
        }
    }

    // Fn + wasd = arrow keys
    if (fn) {
        const char *up = vt.app_cursor_keys ? "\033OA" : "\033[A";
        const char *down = vt.app_cursor_keys ? "\033OB" : "\033[B";
        const char *left = vt.app_cursor_keys ? "\033OD" : "\033[D";
        const char *right = vt.app_cursor_keys ? "\033OC" : "\033[C";
        const char *arrow = NULL;
        if (key == 'w' || key == 'W') arrow = up;
        else if (key == 's' || key == 'S') arrow = down;
        else if (key == 'a' || key == 'A') arrow = left;
        else if (key == 'd' || key == 'D') arrow = right;
        if (arrow) {
            serial_write(arrow, strlen(arrow));
            return;
        }
        if (key == 'q' || key == 'Q') {
            serial_write("\t", 1);
            return;
        }
    }

    // Ctrl + key
    if (ctrl && key >= 32 && key <= 126) {
        char ctrl_ch = (char)(key & 0x1F);
        serial_write(&ctrl_ch, 1);
        return;
    }

    // Alt + key
    if (alt) {
        serial_write("\e", 1);
        serial_write(&key, 1);
        return;
    }

    // Special keys
    switch (key) {
        case '\n':
        case '\r':
            if (vt.line_feed_mode) {
                serial_write("\r\n", 2);
            } else {
                serial_write("\r", 1);
            }
            return;
        case '\b':
            serial_write("\b", 1);
            return;
        case '\t':
            serial_write("\t", 1);
            return;
        case 27:
        case 5:  // Escape key
            serial_write("\e", 1);
            return;
        default:
            break;
    }

    // Regular character
    if (key >= 32 && key <= 126) {
        serial_write(&key, 1);
    }
}

void app_init(app_context_t *ctx) {
    os_log(TAG, "Terminado initializing");

    // Subscribe to keyboard, timer, and serial events
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER | EVENT_SERIAL;
    ctx->timer_interval_ms = 50;  // 20Hz refresh

    // Configure serial port. Default remains 19200 8N1.
    load_terminal_config();
    apply_serial_config(&term_cfg);

    // Initialize VT100 terminal
    vt100_init(&vt, write_to_serial);
    vt100_set_title_callback(&vt, on_title_change);

    // Initialize last-screen tracking
    memset(last_screen, 0, sizeof(last_screen));
    memset(last_attrs, 0, sizeof(last_attrs));
    prev_cursor_x = -1;
    prev_cursor_y = -1;

    cursor_blink_state = 1;
    last_status_line[0] = '\0';
    status_dirty = 1;

    // Set terminal size to match our display
    vt100_set_geometry(&vt, TERM_COLS, TERM_ROWS);

    // Clear display
    display_fill_rect(0, 0, 320, 240, ansi_colors[0]);
    draw_status_bar();

    os_log(TAG, "Terminado ready on %dx%d", TERM_COLS, TERM_ROWS);
}

void app_checkpoint(app_context_t *ctx) {
    os_log(TAG, "Terminado checkpoint");
    save_terminal_config();
}

void app_close(app_context_t *ctx) {
    os_log(TAG, "Terminado closing");
    save_terminal_config();
    serial_deinit();
    display_fill_rect(0, 0, 320, 240, 0x0000);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_SERIAL) {
        for (size_t i = 0; i < event->serial.len; i++) {
            vt100_process(&vt, event->serial.data[i]);
        }
    }

    if (event->type == EVENT_KEYBOARD) {
        if (event->keyboard.pressed) {
            char key = event->keyboard.key;
            int ctrl = (event->keyboard.modifiers & MODIFIER_CTRL) != 0;
            int alt = (event->keyboard.modifiers & MODIFIER_ALT) != 0;
            int fn = (event->keyboard.modifiers & MODIFIER_FN) != 0;
            int fn2 = (event->keyboard.modifiers & MODIFIER_FN2) != 0;

            if (menu_active) {
                handle_config_menu_key(key);
                return;
            }

            // Use Fn2+ESC for config menu, avoiding OS-level Fn+ESC screenshot shortcut.
            if ((key == 27 || key == 5) && fn2) {
                open_config_menu();
                return;
            }

            send_key(key, ctrl, alt, fn, fn2);
        }
    }

    if (event->type == EVENT_TIMER) {
        // Toggle cursor blink (500ms at 50Hz)
        static int blink_counter = 0;
        blink_counter++;
        if (blink_counter >= 10) {
            blink_counter = 0;
            cursor_blink_state = !cursor_blink_state;
        }
    }

    // Render if anything changed
    if (!menu_active && (vt.needs_redraw || event->type == EVENT_TIMER)) {
        render_terminal();
    }
}
