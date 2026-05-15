#include "os_core.h"
#include "text_mode.h"
#include "wifi.h"
#include "core_json.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "clock";

#define WEATHER_FORECAST_URL_FMT "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,weather_code&timezone=%s"
#define WEATHER_GEOCODE_URL_FMT "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json"
#define SETTINGS_KEY_TIMEZONE "time/timezone"
#define SETTINGS_KEY_LOCATION "weather/location"
#define DEFAULT_TIMEZONE "UTC"
#define DEFAULT_LOCATION "40.4168,-3.7038"
#define WEATHER_INITIAL_DELAY_SECONDS 10
#define WEATHER_HTTP_TIMEOUT_MS 15000
#define WEATHER_HTTP_ATTEMPTS 3
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
    time_t warmup_until;
    char status[64];
    char debug[96];
} weather_state_t;

static weather_state_t weather = {
    .has_data = false,
    .temperature_tenths_c = 0,
    .weather_code = -1,
    .next_refresh_at = 0,
    .warmup_until = 0,
    .status = "No weather yet",
    .debug = "Weather dbg: idle",
};

static void trim_spaces(char *text) {
    if (!text || !text[0]) {
        return;
    }

    size_t start = 0;
    while (text[start] == ' ' || text[start] == '\t') {
        start++;
    }

    if (start > 0) {
        memmove(text, text + start, strlen(text + start) + 1);
    }

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len--;
    }
}

static int parse_timezone_offset_seconds(const char *timezone, int *out_seconds) {
    if (!timezone || !out_seconds) {
        return 0;
    }

    const char *text = timezone;
    if (strncmp(text, "UTC", 3) == 0 || strncmp(text, "GMT", 3) == 0) {
        text += 3;
    }

    if (*text == '\0') {
        *out_seconds = 0;
        return 1;
    }

    int sign = 1;
    if (*text == '+') {
        sign = 1;
        text++;
    } else if (*text == '-') {
        sign = -1;
        text++;
    } else {
        return 0;
    }

    int hours = 0;
    int hour_digits = 0;
    while (*text >= '0' && *text <= '9' && hour_digits < 2) {
        hours = hours * 10 + (*text - '0');
        text++;
        hour_digits++;
    }
    if (hour_digits == 0) {
        return 0;
    }

    int minutes = 0;
    if (*text == ':') {
        text++;
        if (!(text[0] >= '0' && text[0] <= '9' && text[1] >= '0' && text[1] <= '9')) {
            return 0;
        }
        minutes = (text[0] - '0') * 10 + (text[1] - '0');
        text += 2;
    } else if (*text >= '0' && *text <= '9') {
        if (!(text[0] >= '0' && text[0] <= '9' && text[1] >= '0' && text[1] <= '9')) {
            return 0;
        }
        minutes = (text[0] - '0') * 10 + (text[1] - '0');
        text += 2;
    }

    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if (*text != '\0') {
        return 0;
    }

    if (hours > 23 || minutes > 59) {
        return 0;
    }

    *out_seconds = sign * (hours * 3600 + minutes * 60);
    return 1;
}

static int is_leap_year(int year) {
    if (year % 400 == 0) {
        return 1;
    }
    if (year % 100 == 0) {
        return 0;
    }
    return (year % 4) == 0;
}

static int days_in_month(int year, int month) {
    static const int month_days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return month_days[month - 1];
}

static void apply_timezone_offset(const os_time_status_t *utc, int offset_seconds, os_time_status_t *out) {
    if (!utc || !out) {
        return;
    }

    *out = *utc;

    int total = utc->hour * 3600 + utc->minute * 60 + utc->second + offset_seconds;
    int day_delta = 0;
    while (total < 0) {
        total += 86400;
        day_delta--;
    }
    while (total >= 86400) {
        total -= 86400;
        day_delta++;
    }

    out->hour = total / 3600;
    out->minute = (total % 3600) / 60;
    out->second = total % 60;

    if (day_delta != 0) {
        out->weekday = (utc->weekday + day_delta) % 7;
        if (out->weekday < 0) {
            out->weekday += 7;
        }

        int year = utc->year;
        int month = utc->month;
        int day = utc->day + day_delta;

        while (day < 1) {
            month--;
            if (month < 1) {
                month = 12;
                year--;
            }
            day += days_in_month(year, month);
        }

        while (day > days_in_month(year, month)) {
            day -= days_in_month(year, month);
            month++;
            if (month > 12) {
                month = 1;
                year++;
            }
        }

        out->year = year;
        out->month = month;
        out->day = day;
    }
}

static void url_encode_basic(const char *src, char *dst, size_t dst_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;

    if (!src || !dst || dst_size == 0) {
        return;
    }

    for (size_t index = 0; src[index] != '\0' && out + 1 < dst_size; index++) {
        unsigned char ch = (unsigned char)src[index];
        bool safe = (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            dst[out++] = (char)ch;
        } else {
            if (out + 3 >= dst_size) {
                break;
            }
            dst[out++] = '%';
            dst[out++] = hex[(ch >> 4) & 0x0F];
            dst[out++] = hex[ch & 0x0F];
        }
    }
    dst[out] = '\0';
}

static int parse_location_lat_lon(const char *location, char *lat_out, size_t lat_size, char *lon_out, size_t lon_size) {
    if (!location || !lat_out || !lon_out || lat_size == 0 || lon_size == 0) {
        return 0;
    }

    const char *comma = strchr(location, ',');
    if (!comma) {
        return 0;
    }

    size_t lat_len = (size_t)(comma - location);
    size_t lon_len = strlen(comma + 1);
    if (lat_len == 0 || lon_len == 0 || lat_len >= lat_size || lon_len >= lon_size) {
        return 0;
    }

    memcpy(lat_out, location, lat_len);
    lat_out[lat_len] = '\0';
    memcpy(lon_out, comma + 1, lon_len);
    lon_out[lon_len] = '\0';

    trim_spaces(lat_out);
    trim_spaces(lon_out);

    return lat_out[0] != '\0' && lon_out[0] != '\0';
}

static int resolve_location(const char *location, char *lat_out, size_t lat_size, char *lon_out, size_t lon_size) {
    if (parse_location_lat_lon(location, lat_out, lat_size, lon_out, lon_size)) {
        return 1;
    }

    char encoded[128];
    char geocode_url[256];
    char geocode_response[1024];

    url_encode_basic(location, encoded, sizeof(encoded));
    if (encoded[0] == '\0') {
        return 0;
    }

    snprintf(geocode_url, sizeof(geocode_url), WEATHER_GEOCODE_URL_FMT, encoded);
    int geocode_result = os_http_get(geocode_url, geocode_response, sizeof(geocode_response), WEATHER_HTTP_TIMEOUT_MS);
    if (geocode_result <= 0) {
        return 0;
    }

    if (JSON_Validate(geocode_response, strlen(geocode_response)) != JSONSuccess) {
        return 0;
    }

    const char *lat_value = NULL;
    const char *lon_value = NULL;
    size_t lat_len = 0;
    size_t lon_len = 0;
    if (JSON_SearchConst(geocode_response, strlen(geocode_response), "results[0].latitude", strlen("results[0].latitude"), &lat_value, &lat_len, NULL) != JSONSuccess ||
        JSON_SearchConst(geocode_response, strlen(geocode_response), "results[0].longitude", strlen("results[0].longitude"), &lon_value, &lon_len, NULL) != JSONSuccess) {
        return 0;
    }

    if (lat_len >= lat_size || lon_len >= lon_size) {
        return 0;
    }

    memcpy(lat_out, lat_value, lat_len);
    lat_out[lat_len] = '\0';
    memcpy(lon_out, lon_value, lon_len);
    lon_out[lon_len] = '\0';
    return 1;
}

static void weather_pause_seconds(int seconds) {
    time_t end_time = time(NULL) + seconds;
    while (time(NULL) < end_time) {
        for (volatile int spin = 0; spin < 50000; spin++) {
        }
    }
}

static int json_parse_int_value(const char *text, int *out_value) {
    int sign = 1;
    int value = 0;
    int saw_digit = 0;

    if (!text || !out_value) {
        return 0;
    }

    if (*text == '-') {
        sign = -1;
        text++;
    }

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
        saw_digit = 1;
    }

    if (!saw_digit) {
        return 0;
    }

    *out_value = value * sign;
    return 1;
}

static int json_parse_tenths_value(const char *text, int *out_tenths) {
    int sign = 1;
    int whole = 0;
    int saw_digit = 0;

    if (!text || !out_tenths) {
        return 0;
    }

    if (*text == '-') {
        sign = -1;
        text++;
    }

    while (*text >= '0' && *text <= '9') {
        whole = whole * 10 + (*text - '0');
        text++;
        saw_digit = 1;
    }

    if (!saw_digit) {
        return 0;
    }

    int tenths = whole * 10;
    if (*text == '.' && text[1] >= '0' && text[1] <= '9') {
        tenths += text[1] - '0';
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
    if (!force && weather.warmup_until > 0 && now < weather.warmup_until) {
        snprintf(weather.status, sizeof(weather.status), "Weather warming up");
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: warmup until %lld", (long long)weather.warmup_until);
        os_log(TAG, "Weather refresh delayed: warmup_until=%lld now=%lld",
               (long long)weather.warmup_until, (long long)now);
        weather.next_refresh_at = weather.warmup_until;
        return;
    }

    if (!force && now < weather.next_refresh_at) {
        os_log(TAG, "Weather refresh skipped: next=%lld now=%lld status=%s",
               (long long)weather.next_refresh_at, (long long)now, weather.status);
        return;
    }

    os_log(TAG, "Weather refresh start: force=%d now=%lld wifi=%d",
           force, (long long)now, wifi_is_connected());
    snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: refresh start");

    if (!wifi_is_connected()) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather: WiFi disconnected");
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: wifi disconnected");
        os_log(TAG, "Weather refresh aborted: WiFi disconnected");
        weather.next_refresh_at = now + 30;
        return;
    }

    char timezone[48];
    char timezone_encoded[96];
    char location[64];
    char latitude[24];
    char longitude[24];
    char weather_url[320];

    os_settings_get_string(SETTINGS_KEY_TIMEZONE, DEFAULT_TIMEZONE, timezone, sizeof(timezone));
    os_settings_get_string(SETTINGS_KEY_LOCATION, DEFAULT_LOCATION, location, sizeof(location));
    trim_spaces(timezone);
    trim_spaces(location);
    if (timezone[0] == '\0') {
        snprintf(timezone, sizeof(timezone), "%s", DEFAULT_TIMEZONE);
    }
    if (location[0] == '\0') {
        snprintf(location, sizeof(location), "%s", DEFAULT_LOCATION);
    }

    if (!resolve_location(location, latitude, sizeof(latitude), longitude, sizeof(longitude))) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather location unresolved");
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: location=%s", location);
        os_log(TAG, "Weather location resolve failed: %s", location);
        weather.next_refresh_at = now + 30;
        return;
    }

    url_encode_basic(timezone, timezone_encoded, sizeof(timezone_encoded));
    snprintf(weather_url, sizeof(weather_url), WEATHER_FORECAST_URL_FMT, latitude, longitude, timezone_encoded);

    char response[768];
    int result = -1;
    for (int attempt = 1; attempt <= WEATHER_HTTP_ATTEMPTS; attempt++) {
        response[0] = '\0';
        result = os_http_get(weather_url, response, sizeof(response), WEATHER_HTTP_TIMEOUT_MS);
        os_log(TAG, "Weather HTTP attempt=%d result=%d bytes=%u", attempt, result, (unsigned)strlen(response));
        if (result > 0) {
            break;
        }
        if (attempt < WEATHER_HTTP_ATTEMPTS) {
            weather_pause_seconds(1);
        }
    }
    if (result <= 0) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather fetch failed (%d)", result);
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: fetch failed %d", result);
        os_log(TAG, "Weather fetch failed after %d attempts: result=%d", WEATHER_HTTP_ATTEMPTS, result);
        weather.next_refresh_at = now + 15;
        return;
    }

    JSONStatus_t json_status = JSON_Validate(response, strlen(response));
    os_log(TAG, "Weather JSON validate=%d", json_status);
    if (json_status != JSONSuccess) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather parse failed");
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: validate failed %d", json_status);
        os_log(TAG, "Weather parse failed at validate: %d", json_status);
        weather.next_refresh_at = now + 60;
        return;
    }

    const char * temp_value = NULL;
    const char * code_value = NULL;
    size_t temp_length = 0;
    size_t code_length = 0;
    if (JSON_SearchConst(response, strlen(response), "current.temperature_2m", strlen("current.temperature_2m"), &temp_value, &temp_length, NULL) != JSONSuccess ||
        JSON_SearchConst(response, strlen(response), "current.weather_code", strlen("current.weather_code"), &code_value, &code_length, NULL) != JSONSuccess) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather parse failed");
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: search failed");
        os_log(TAG, "Weather parse failed: current fields not found");
        weather.next_refresh_at = now + 60;
        return;
    }

    int temp_tenths = 0;
    int code = 0;
    char temp_buffer[32];
    char code_buffer[16];
    if (temp_length >= sizeof(temp_buffer) || code_length >= sizeof(code_buffer)) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather parse failed");
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: value too long");
        os_log(TAG, "Weather parse failed: value length too long");
        weather.next_refresh_at = now + 60;
        return;
    }

    memcpy(temp_buffer, temp_value, temp_length);
    temp_buffer[temp_length] = '\0';
    memcpy(code_buffer, code_value, code_length);
    code_buffer[code_length] = '\0';

    if (!json_parse_tenths_value(temp_buffer, &temp_tenths) ||
        !json_parse_int_value(code_buffer, &code)) {
        weather.has_data = false;
        snprintf(weather.status, sizeof(weather.status), "Weather parse failed");
        snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: value parse failed");
        os_log(TAG, "Weather parse failed at values: temp=%s code=%s", temp_buffer, code_buffer);
        weather.next_refresh_at = now + 60;
        return;
    }

    weather.has_data = true;
    weather.temperature_tenths_c = temp_tenths;
    weather.weather_code = code;
    snprintf(weather.status, sizeof(weather.status), "Weather updated");
    snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: %s %s", timezone, location);
    os_log(TAG, "Weather updated: temp_tenths=%d weather_code=%d", temp_tenths, code);
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
    char timezone[48];
    os_settings_get_string(SETTINGS_KEY_TIMEZONE, DEFAULT_TIMEZONE, timezone, sizeof(timezone));
    trim_spaces(timezone);
    if (timezone[0] == '\0') {
        snprintf(timezone, sizeof(timezone), "%s", DEFAULT_TIMEZONE);
    }

    int tz_offset_seconds = 0;
    int has_tz_offset = parse_timezone_offset_seconds(timezone, &tz_offset_seconds);

    os_time_status_t display_status = time_status;
    if (has_tz_offset) {
        apply_timezone_offset(&time_status, tz_offset_seconds, &display_status);
    }

    snprintf(line, sizeof(line), "%-14s", timezone);
    text_mode_print_at_attr(10, 0, line, TEXT_COLOR_YELLOW, TEXT_ATTR_BOLD);

    /* Text rows 18+ are safely below the large time area */
    snprintf(line, sizeof(line), "%04d-%02d-%02d", display_status.year, display_status.month, display_status.day);
    print_padded_line(2, 18, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD, line, 20);

    static const char *weekday_names[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    const char *weekday = "Unknown";
    if (display_status.weekday >= 0 && display_status.weekday < 7) {
        weekday = weekday_names[display_status.weekday];
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

    print_padded_line(2, 28, TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL, weather.debug, 44);

    /* Flush text-mode cells first, then paint the large clock cells on top */
    text_mode_flush();
    draw_large_time(&display_status);
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_TIMER | EVENT_KEYBOARD;
    ctx->timer_interval_ms = 1000;

    text_mode_init();
    weather.warmup_until = time(NULL) + WEATHER_INITIAL_DELAY_SECONDS;
    weather.next_refresh_at = weather.warmup_until;
    snprintf(weather.status, sizeof(weather.status), "Weather warming up");
    snprintf(weather.debug, sizeof(weather.debug), "Weather dbg: init warmup until %lld", (long long)weather.warmup_until);
    os_log(TAG, "Weather init warmup_until=%lld", (long long)weather.warmup_until);
    os_log(TAG, "Weather init status: %s", weather.status);
    os_log(TAG, "Weather init debug: %s", weather.debug);
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