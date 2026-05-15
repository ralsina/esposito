#include "os_core.h"
#include "text_mode.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "clock";

static void print_padded_line(int x, int y, uint8_t color, uint8_t attr, const char *text, int width) {
    char line[96];
    int text_len = snprintf(line, sizeof(line), "%-*.*s", width, width, text ? text : "");
    if (text_len < 0) {
        return;
    }
    text_mode_print_at_attr(x, y, line, color, attr);
}

static void draw_static_clock(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(2, 1, "Clock", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(10, 1, "UTC", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 12, "Time status:", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 17, "Updates every second", TEXT_COLOR_BRIGHT_BLUE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 18, "Ctrl+Esc returns to launcher", TEXT_COLOR_BRIGHT_BLUE, TEXT_ATTR_NORMAL);
}

static void draw_clock(void) {
    os_time_status_t time_status;
    if (!os_get_time_status(&time_status)) {
        print_padded_line(2, 4, TEXT_COLOR_RED, TEXT_ATTR_NORMAL, "Failed to read time status", 34);
        text_mode_flush();
        return;
    }

    char line[80];

    snprintf(line, sizeof(line), "%04d-%02d-%02d", time_status.year, time_status.month, time_status.day);
    print_padded_line(2, 4, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD, line, 20);

    snprintf(line, sizeof(line), "%02d:%02d:%02d", time_status.hour, time_status.minute, time_status.second);
    print_padded_line(2, 6, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_BOLD, line, 20);

    static const char *weekday_names[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    const char *weekday = "Unknown";
    if (time_status.weekday >= 0 && time_status.weekday < 7) {
        weekday = weekday_names[time_status.weekday];
    }
    snprintf(line, sizeof(line), "Day: %s", weekday);
    print_padded_line(2, 8, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL, line, 24);

    snprintf(line, sizeof(line), "Unix: %lld", (long long)time_status.unix_time);
    print_padded_line(2, 10, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL, line, 28);

    if (time_status.synchronized) {
        print_padded_line(2, 13, TEXT_COLOR_GREEN, TEXT_ATTR_NORMAL, "Trusted (NTP synced this boot)", 34);
        snprintf(line, sizeof(line), "Last sync: %lld", (long long)time_status.last_sync_time);
        print_padded_line(2, 14, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL, line, 32);
    } else {
        print_padded_line(2, 13, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL, "Untrusted (NTP not synced this boot)", 36);
        print_padded_line(2, 14, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL, "Last sync: never", 32);
    }

    text_mode_flush();
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_TIMER | EVENT_KEYBOARD;
    ctx->timer_interval_ms = 1000;

    text_mode_init();
    draw_static_clock();
    draw_clock();
    os_log(TAG, "Clock app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    text_mode_clear(TEXT_COLOR_BLACK);
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;

    if (event->type == EVENT_TIMER) {
        draw_clock();
        return;
    }

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        draw_clock();
    }
}