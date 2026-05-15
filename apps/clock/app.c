#include "os_core.h"
#include "text_mode.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "clock";

#define CLOCK_DIGIT_W 3
#define CLOCK_DIGIT_H 5
#define CLOCK_DIGIT_SCALE 2
#define CLOCK_DIGIT_PITCH 7
#define CLOCK_DIGIT_Y 4

static const uint8_t digit_bits[11][CLOCK_DIGIT_H] = {
    { 0x7, 0x5, 0x5, 0x5, 0x7 }, /* 0 */
    { 0x2, 0x6, 0x2, 0x2, 0x7 }, /* 1 */
    { 0x7, 0x1, 0x7, 0x4, 0x7 }, /* 2 */
    { 0x7, 0x1, 0x7, 0x1, 0x7 }, /* 3 */
    { 0x5, 0x5, 0x7, 0x1, 0x1 }, /* 4 */
    { 0x7, 0x4, 0x7, 0x1, 0x7 }, /* 5 */
    { 0x7, 0x4, 0x7, 0x5, 0x7 }, /* 6 */
    { 0x7, 0x1, 0x2, 0x2, 0x2 }, /* 7 */
    { 0x7, 0x5, 0x7, 0x5, 0x7 }, /* 8 */
    { 0x7, 0x5, 0x7, 0x1, 0x7 }, /* 9 */
    { 0x0, 0x2, 0x0, 0x2, 0x0 }, /* : */
};

static void print_padded_line(int x, int y, uint8_t color, uint8_t attr, const char *text, int width) {
    char line[96];
    int text_len = snprintf(line, sizeof(line), "%-*.*s", width, width, text ? text : "");
    if (text_len < 0) {
        return;
    }
    text_mode_print_at_attr(x, y, line, color, attr);
}

#define LARGE_TIME_Y 4

static void clear_large_digit(int x, int y) {
    for (int row = 0; row < CLOCK_DIGIT_H * CLOCK_DIGIT_SCALE; row++) {
        for (int col = 0; col < CLOCK_DIGIT_W * CLOCK_DIGIT_SCALE; col++) {
            text_mode_print_at_attr_bg(x + col, y + row, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
        }
    }
}

static int clock_char_index(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch == ':') {
        return 10;
    }
    return -1;
}

static void draw_large_digit(int x, int y, char ch) {
    int digit_index = clock_char_index(ch);
    clear_large_digit(x, y);
    if (digit_index < 0) {
        return;
    }

    for (int row = 0; row < CLOCK_DIGIT_H; row++) {
        uint8_t row_bits = digit_bits[digit_index][row];
        for (int col = 0; col < CLOCK_DIGIT_W; col++) {
            bool lit = (row_bits & (1 << (CLOCK_DIGIT_W - 1 - col))) != 0;
            if (!lit) {
                continue;
            }

            int cell_x = x + col * CLOCK_DIGIT_SCALE;
            int cell_y = y + row * CLOCK_DIGIT_SCALE;
            for (int dy = 0; dy < CLOCK_DIGIT_SCALE; dy++) {
                for (int dx = 0; dx < CLOCK_DIGIT_SCALE; dx++) {
                    text_mode_print_at_attr_bg(cell_x + dx, cell_y + dy, " ", TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_INVERSE);
                }
            }
        }
    }
}

static void draw_static_clock(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(2, 0, "Clock", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(10, 0, "UTC", TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 16, "Time status:", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
}

static void draw_large_time(const os_time_status_t *time_status) {
    static char prev_time_line[32] = "";

    char time_line[32];
    snprintf(time_line, sizeof(time_line), "%02d:%02d:%02d",
             time_status->hour, time_status->minute, time_status->second);

    const int digit_width = CLOCK_DIGIT_W * CLOCK_DIGIT_SCALE;
    const int digit_pitch = CLOCK_DIGIT_PITCH;
    const int time_width = (int)strlen(time_line) * digit_pitch - (digit_pitch - digit_width);
    int screen_width = text_mode_get_cols();
    int start_x = (screen_width - time_width) / 2;
    if (start_x < 0) start_x = 0;

    for (size_t i = 0; time_line[i]; i++) {
        if (prev_time_line[i] == time_line[i]) {
            continue;
        }
        draw_large_digit(start_x + (int)i * digit_pitch, LARGE_TIME_Y, time_line[i]);
    }

    strncpy(prev_time_line, time_line, sizeof(prev_time_line) - 1);
    prev_time_line[sizeof(prev_time_line) - 1] = '\0';
}

static void draw_clock(void) {
    os_time_status_t time_status;
    if (!os_get_time_status(&time_status)) {
        print_padded_line(2, 18, TEXT_COLOR_RED, TEXT_ATTR_NORMAL, "Failed to read time status", 34);
        text_mode_flush();
        return;
    }

    char line[80];

    /* Text rows 18+ are safely below the large time area */
    snprintf(line, sizeof(line), "%04d-%02d-%02d", time_status.year, time_status.month, time_status.day);
    print_padded_line(2, 18, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD, line, 20);

    static const char *weekday_names[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    const char *weekday = "Unknown";
    if (time_status.weekday >= 0 && time_status.weekday < 7) {
        weekday = weekday_names[time_status.weekday];
    }
    snprintf(line, sizeof(line), "%s", weekday);
    print_padded_line(2, 20, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL, line, 16);

    snprintf(line, sizeof(line), "Unix: %lld", (long long)time_status.unix_time);
    print_padded_line(2, 22, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL, line, 28);

    if (time_status.synchronized) {
        print_padded_line(2, 25, TEXT_COLOR_GREEN, TEXT_ATTR_NORMAL, "Trusted (NTP synced)", 24);
        snprintf(line, sizeof(line), "Last sync: %lld", (long long)time_status.last_sync_time);
        print_padded_line(2, 26, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL, line, 32);
    } else {
        print_padded_line(2, 25, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL, "Untrusted (no NTP sync)", 28);
        print_padded_line(2, 26, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL, "Last sync: never", 32);
    }

    /* Flush text-mode cells first, then paint the large clock cells on top */
    text_mode_flush();
    draw_large_time(&time_status);
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