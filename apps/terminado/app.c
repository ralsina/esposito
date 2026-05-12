#include "os_core.h"
#include "hardware.h"
#include "vt100.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "terminado";

// Terminal dimensions (matches text mode 5x8 font on 320x240 display)
#define TERM_COLS 64
#define TERM_ROWS 30
#define CELL_W 5
#define CELL_H 8

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

// Write callback: send data to serial host
static void write_to_serial(const char *data, size_t len) {
    serial_write(data, len);
}

// Title callback
static void on_title_change(const char *title) {
    os_log(TAG, "Title: %s", title);
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
            char buf[2] = {ch, '\0'};
            display_draw_text(px, py, buf, bg);
        }
    } else {
        // Restore normal cell
        display_fill_rect(px, py, CELL_W, CELL_H, bg);
        if (ch != ' ') {
            if (attr.bold) fg = brighten_rgb565(fg);
            display_draw_char_at(px, py, ch, fg, bg);
        }
    }
}

// Render changed VT100 cells to display
static void render_terminal(void) {
    // If screen reverse mode changed, force full repaint
    if (vt.screen_reverse != last_screen_reverse) {
        last_screen_reverse = vt.screen_reverse;
        memset(last_screen, 0, sizeof(last_screen));
        memset(last_attrs, 0, sizeof(last_attrs));
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

                int px = x * CELL_W;
                int py = y * CELL_H;

                uint16_t fg = ansi_colors[attr.fg & 7];
                uint16_t bg = ansi_colors[attr.bg & 7];
                int rev = attr.reverse ^ vt.screen_reverse;
                if (rev) { uint16_t t = fg; fg = bg; bg = t; }

                display_fill_rect(px, py, CELL_W, CELL_H, bg);

                if (c != ' ') {
                    if (attr.bold) fg = brighten_rgb565(fg);
                    display_draw_char_at(px, py, c, fg, bg);

                    if (attr.underline) {
                        display_fill_rect(px, py + CELL_H - 1, CELL_W, 1, fg);
                    }
                }
            }
        }
    }

    render_cursor();
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

    // Configure serial port
    serial_init(19200, 8, 'N', 1);

    // Initialize VT100 terminal
    vt100_init(&vt, write_to_serial);
    vt100_set_title_callback(&vt, on_title_change);

    // Initialize last-screen tracking
    memset(last_screen, 0, sizeof(last_screen));
    memset(last_attrs, 0, sizeof(last_attrs));

    cursor_blink_state = 1;

    // Set terminal size to match our display
    vt100_set_geometry(&vt, TERM_COLS, TERM_ROWS);

    // Clear display
    display_fill_rect(0, 0, 320, 240, ansi_colors[0]);

    os_log(TAG, "Terminado ready on %dx%d", TERM_COLS, TERM_ROWS);
}

void app_checkpoint(app_context_t *ctx) {
    os_log(TAG, "Terminado checkpoint");
}

void app_close(app_context_t *ctx) {
    os_log(TAG, "Terminado closing");
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
    if (vt.needs_redraw || event->type == EVENT_TIMER) {
        render_terminal();
    }
}
