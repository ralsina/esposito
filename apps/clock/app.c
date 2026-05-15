#include "os_core.h"
#include "text_mode.h"
#include "wifi.h"
#include "jsmn.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "clock";

#define WEATHER_URL "http://api.open-meteo.com/v1/forecast?latitude=40.4168&longitude=-3.7038&current=temperature_2m,weather_code&timezone=UTC"
#define WEATHER_REFRESH_SECONDS 600

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

typedef struct {
    bool has_data;
    int temperature_tenths_c;
    int weather_code;
    time_t next_refresh_at;
    char status[64];
} weather_state_t;

static weather_state_t weather = {
    .has_data = false,
    .temperature_tenths_c = 0,
    .weather_code = -1,
    .next_refresh_at = 0,
    .status = "No weather yet",
};

static int json_token_equals(const char *json, const jsmntok_t *token, const char *text) {
    if (!json || !token || !text || token->start < 0 || token->end < token->start) {
        return 0;
    }

    size_t token_len = (size_t)(token->end - token->start);
    size_t text_len = strlen(text);
    if (token_len != text_len) {
        return 0;
    }

    return strncmp(json + token->start, text, token_len) == 0;
}

static int json_skip_token_tree(const jsmntok_t *tokens, int token_count, int index) {
    if (!tokens || index < 0 || index >= token_count) {
        return token_count;
    }

    int next = index + 1;
    switch (tokens[index].type) {
        case JSMN_OBJECT:
            for (int pair_index = 0; pair_index < tokens[index].size; pair_index++) {
                next = json_skip_token_tree(tokens, token_count, next);
                next = json_skip_token_tree(tokens, token_count, next);
            }
            break;
        case JSMN_ARRAY:
            for (int item_index = 0; item_index < tokens[index].size; item_index++) {
                next = json_skip_token_tree(tokens, token_count, next);
            }
            break;
        default:
            break;
    }
    return next;
}

static int json_find_object_value(const char *json,
                                  const jsmntok_t *tokens,
                                  int token_count,
                                  int object_token_index,
                                  const char *key,
                                  int *value_token_index) {
    if (!json || !tokens || !key || !value_token_index ||
        object_token_index < 0 || object_token_index >= token_count ||
        tokens[object_token_index].type != JSMN_OBJECT) {
        return 0;
    }

    int index = object_token_index + 1;
    for (int pair_index = 0; pair_index < tokens[object_token_index].size; pair_index++) {
        if (index + 1 >= token_count) {
            return 0;
        }

        int key_index = index;
        int value_index = key_index + 1;
        if (tokens[key_index].type == JSMN_STRING && json_token_equals(json, &tokens[key_index], key)) {
            *value_token_index = value_index;
            return 1;
        }

        index = json_skip_token_tree(tokens, token_count, value_index);
    }

    return 0;
}

static int json_parse_int_token(const char *json, const jsmntok_t *token, int *out_value) {
    if (!json || !token || !out_value || token->type != JSMN_PRIMITIVE || token->start < 0 || token->end <= token->start) {
        return 0;
    }

    int index = token->start;
    int sign = 1;
    if (json[index] == '-') {
        sign = -1;
        index++;
    }

    int value = 0;
    int saw_digit = 0;
    while (index < token->end && json[index] >= '0' && json[index] <= '9') {
        value = value * 10 + (json[index] - '0');
        index++;
        saw_digit = 1;
    }

    if (!saw_digit) {
        return 0;
    }

    *out_value = value * sign;
    return 1;
}

static int json_parse_tenths_token(const char *json, const jsmntok_t *token, int *out_tenths) {
    if (!json || !token || !out_tenths || token->type != JSMN_PRIMITIVE || token->start < 0 || token->end <= token->start) {
        return 0;
    }

    int index = token->start;
    int sign = 1;
    if (json[index] == '-') {
        sign = -1;
        index++;
    }

    int whole = 0;
    int saw_digit = 0;
    while (index < token->end && json[index] >= '0' && json[index] <= '9') {
        whole = whole * 10 + (json[index] - '0');
        index++;
        saw_digit = 1;
    }

    if (!saw_digit) {
        return 0;
    }

    int tenths = whole * 10;
    if (index < token->end && json[index] == '.' && index + 1 < token->end &&
        json[index + 1] >= '0' && json[index + 1] <= '9') {
        tenths += (json[index + 1] - '0');
    }

    *out_tenths = tenths * sign;
    return 1;
}

static const char *weather_code_label(int code) {
    switch (code) {
        case 0: return "Clear";
        case 1:
        case 2:
        case 3: return "Cloudy";
        case 45:
        case 48: return "Fog";
        case 51:
        case 53:
        case 55: return "Drizzle";
        case 61:
        case 63:
        case 65: return "Rain";
        case 71:
        case 73:
        case 75: return "Snow";
        case 95:
        case 96:
        case 99: return "Storm";
        default: return "Unknown";
    }
}

static void refresh_weather_if_needed(int force) {
    time_t now = time(NULL);
    if (!force && now < weather.next_refresh_at) {
        return;
    }

    if (!wifi_is_connected()) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather: WiFi disconnected");
        weather.next_refresh_at = now + 30;
        return;
    }

    char response[768];
    int result = os_http_get(WEATHER_URL, response, sizeof(response), 5000);
    if (result <= 0) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather fetch failed (%d)", result);
        weather.next_refresh_at = now + 60;
        return;
    }

    jsmn_parser parser;
    jsmntok_t tokens[128];
    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, response, (unsigned int)strlen(response), tokens, 128);
    if (token_count < 1 || tokens[0].type != JSMN_OBJECT) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather parse failed");
        weather.next_refresh_at = now + 60;
        return;
    }

    int current_token_index = -1;
    int temp_token_index = -1;
    int code_token_index = -1;
    if (!json_find_object_value(response, tokens, token_count, 0, "current", &current_token_index) ||
        current_token_index < 0 || current_token_index >= token_count ||
        tokens[current_token_index].type != JSMN_OBJECT ||
        !json_find_object_value(response, tokens, token_count, current_token_index, "temperature_2m", &temp_token_index) ||
        !json_find_object_value(response, tokens, token_count, current_token_index, "weather_code", &code_token_index)) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather parse failed");
        weather.next_refresh_at = now + 60;
        return;
    }

    int temp_tenths = 0;
    int code = 0;
    if (!json_parse_tenths_token(response, &tokens[temp_token_index], &temp_tenths) ||
        !json_parse_int_token(response, &tokens[code_token_index], &code)) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather parse failed");
        weather.next_refresh_at = now + 60;
        return;
    }

    weather.has_data = true;
    weather.temperature_tenths_c = temp_tenths;
    weather.weather_code = code;
    snprintf(weather.status, sizeof(weather.status), "Weather updated");
    weather.next_refresh_at = now + WEATHER_REFRESH_SECONDS;
}

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
    refresh_weather_if_needed(0);

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

    if (weather.has_data) {
        int abs_t = weather.temperature_tenths_c < 0 ? -weather.temperature_tenths_c : weather.temperature_tenths_c;
        int whole = abs_t / 10;
        int frac = abs_t % 10;
        char sign = weather.temperature_tenths_c < 0 ? '-' : '\0';
        if (sign) {
            snprintf(line, sizeof(line), "Weather: %c%d.%d C  %s", sign, whole, frac, weather_code_label(weather.weather_code));
        } else {
            snprintf(line, sizeof(line), "Weather: %d.%d C  %s", whole, frac, weather_code_label(weather.weather_code));
        }
        print_padded_line(2, 27, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL, line, 40);
    } else {
        print_padded_line(2, 27, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL, weather.status, 40);
    }

    /* Flush text-mode cells first, then paint the large clock cells on top */
    text_mode_flush();
    draw_large_time(&time_status);
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_TIMER | EVENT_KEYBOARD;
    ctx->timer_interval_ms = 1000;

    text_mode_init();
    refresh_weather_if_needed(1);
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
        if (event->keyboard.key == 'r' || event->keyboard.key == 'R') {
            refresh_weather_if_needed(1);
        }
        draw_clock();
    }
}