#include "os_core.h"
#include "text_mode.h"
#include "battery.h"
#include <stdio.h>

static const char *TAG = "battery";

#define MIN_MV 3300
#define MAX_MV 4200
#define BAR_WIDTH 20
#define BTN_LABEL "Exit"
#define BTN_W 6
#define BTN_H 1

static int button_row;

static int clamp(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void draw_battery_bar(int row, int pct) {
    int filled = (pct * BAR_WIDTH + 50) / 100;
    char bar[BAR_WIDTH + 8];
    int pos = 0;

    bar[pos++] = '[';
    for (int i = 0; i < BAR_WIDTH; i++) {
        bar[pos++] = i < filled ? '#' : '.';
    }
    bar[pos++] = ']';
    bar[pos++] = ' ';
    bar[pos++] = ' ';
    bar[pos++] = (pct / 10) + '0';
    bar[pos++] = (pct % 10) + '0';
    bar[pos++] = '%';
    bar[pos] = '\0';

    text_mode_printf_at((text_mode_get_cols() - 30) / 2, row, "%s", bar);
}

static void draw_exit_button(int cols, int rows) {
    button_row = rows - 1;
    int bx = (cols - BTN_W) / 2;
    text_mode_printf_at_attr(bx, button_row, TEXT_COLOR_WHITE, TEXT_ATTR_INVERSE,
                             " %s ", BTN_LABEL);
}

static bool is_exit_touch(int tx, int ty) {
    int gx, gy;
    text_mode_pixel_to_cell(tx, ty, &gx, &gy);
    int cols = text_mode_get_cols();
    int bx = (cols - BTN_W) / 2;
    return gy == button_row && gx >= bx && gx < bx + BTN_W;
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_TIMER | EVENT_TOUCH;
    ctx->timer_interval_ms = 5000;

    text_mode_init();
    text_mode_clear(TEXT_COLOR_BLACK);

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    text_mode_printf_at((cols - 15) / 2, 3, "== BATTERY ==");

    if (!battery_init()) {
        text_mode_printf_at((cols - 20) / 2, 5, "ADC not available");
        os_log(TAG, "battery_init failed");
        draw_exit_button(cols, rows);
        text_mode_flush();
        return;
    }

    int mv = battery_read_millivolts();
    if (mv < 0) {
        text_mode_printf_at((cols - 18) / 2, 5, "Read failed");
        draw_exit_button(cols, rows);
        text_mode_flush();
        return;
    }

    text_mode_printf_at((cols - 18) / 2, 6, "Voltage: %d.%03d V", mv / 1000, mv % 1000);

    int pct = (clamp(mv, MIN_MV, MAX_MV) - MIN_MV) * 100 / (MAX_MV - MIN_MV);
    draw_battery_bar(8, pct);

    draw_exit_button(cols, rows);
    text_mode_flush();
    os_log(TAG, "initialized: %d mV", mv);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_TIMER) {
        int mv = battery_read_millivolts();
        if (mv < 0) {
            text_mode_printf_at(2, 6, "Read failed       ");
            return;
        }

        int cols = text_mode_get_cols();
        text_mode_printf_at((cols - 18) / 2, 6, "Voltage: %d.%03d V", mv / 1000, mv % 1000);

        int pct = (clamp(mv, MIN_MV, MAX_MV) - MIN_MV) * 100 / (MAX_MV - MIN_MV);
        draw_battery_bar(8, pct);
        text_mode_flush();
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        if (is_exit_touch(event->touch.x, event->touch.y)) {
            os_load_app("launcher");
        }
    }
}

void app_checkpoint(app_context_t *ctx) {
    os_log(TAG, "checkpoint");
}

void app_close(app_context_t *ctx) {
    battery_deinit();
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "closed");
}
