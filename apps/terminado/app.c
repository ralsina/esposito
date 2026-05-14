#include "os_core.h"
#include "hardware.h"
#include "app_config.h"
#include "terminal_mode.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "terminado";

#define TERM_COLS 64
#define TERM_ROWS 29
#define CELL_W 5
#define CELL_H 8

#define MENU_ITEMS 4
#define MENU_X_COL 8
#define MENU_Y_ROW 6
#define MENU_W_COL 48
#define MENU_H_ROW 16

typedef struct {
    int baud;
    int data_bits;
    char parity;
    int stop_bits;
} terminal_config_t;

static terminal_mode_t *term = NULL;
static terminal_config_t term_cfg;
static terminal_config_t menu_cfg;
static int menu_active = 0;
static int menu_selected = 0;
static char terminal_title[48] = "Terminado";

static const uint16_t ansi_colors[8] = {
    0x0000,
    0xF800,
    0x07E0,
    0xFFE0,
    0x001F,
    0xF81F,
    0x07FF,
    0xFFFF,
};

static const int baud_rates[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400};
static const int baud_rates_count = (int)(sizeof(baud_rates) / sizeof(baud_rates[0]));

static int valid_data_bits(int bits) {
    return bits >= 5 && bits <= 8;
}

static int valid_stop_bits(int bits) {
    return bits == 1 || bits == 2;
}

static int valid_parity(char parity) {
    return parity == 'N' || parity == 'E' || parity == 'O';
}

static int baud_index_for_value(int baud) {
    for (int index = 0; index < baud_rates_count; index++) {
        if (baud_rates[index] == baud) {
            return index;
        }
    }
    return 5;
}

static void refresh_status(void) {
    char status[96];
    snprintf(status, sizeof(status), "%s | %d %d%c%d | Fn2+ESC cfg",
             terminal_title,
             term_cfg.baud,
             term_cfg.data_bits,
             term_cfg.parity,
             term_cfg.stop_bits);
    terminal_mode_set_status(term, status);
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
        serial_init(19200, 8, 'N', 1);
    }
}

static void draw_cell_text(int col, int row, const char *text, uint16_t fg, uint16_t bg) {
    int x = col * CELL_W;
    int y = row * CELL_H;
    for (int index = 0; text[index]; index++) {
        display_draw_char_at(x + index * CELL_W, y, text[index], fg, bg);
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

static void terminal_write_to_serial(const char *data, size_t len) {
    serial_write(data, len);
}

static void terminal_title_change(const char *title) {
    os_log(TAG, "Title: %s", title);
    strncpy(terminal_title, title, sizeof(terminal_title) - 1);
    terminal_title[sizeof(terminal_title) - 1] = '\0';
    refresh_status();
}

static void open_config_menu(void) {
    menu_cfg = term_cfg;
    menu_selected = 0;
    menu_active = 1;
    draw_config_menu();
}

static void close_config_menu(void) {
    menu_active = 0;
    terminal_mode_reset(term);
    refresh_status();
    terminal_mode_render(term);
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

void app_init(app_context_t *ctx) {
    os_log(TAG, "Terminado initializing (terminal_mode host)");

    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER | EVENT_SERIAL;
    ctx->timer_interval_ms = 50;

    term = terminal_mode_default();

    load_terminal_config();
    apply_serial_config(&term_cfg);

    terminal_mode_init(term, TERM_COLS, TERM_ROWS, terminal_write_to_serial);
    terminal_mode_set_title_callback(term, terminal_title_change);
    refresh_status();
    terminal_mode_render(term);

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
        terminal_mode_process_bytes(term, event->serial.data, event->serial.len);
    }

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        if (menu_active) {
            handle_config_menu_key(key);
            return;
        }

        if ((key == 27 || key == 5) && (event->keyboard.modifiers & MODIFIER_FN2)) {
            open_config_menu();
            return;
        }

        terminal_mode_handle_key(term, key, event->keyboard.modifiers);
    }

    if (!menu_active && event->type == EVENT_TIMER) {
        terminal_mode_render(term);
    }
}
