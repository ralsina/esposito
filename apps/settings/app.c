#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "wifi.h"
#include "app_config.h"
#include "hardware.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "settings";

typedef enum {
    STATE_MAIN,
    STATE_SCAN_RESULTS,
    STATE_ENTER_SSID,
    STATE_ENTER_PASSWORD,
    STATE_ENTER_TIMEZONE,
    STATE_ENTER_LOCATION,
    STATE_MESSAGE,
} app_state_t;

static app_state_t state = STATE_MAIN;
static char input_ssid[WIFI_MAX_SSID] = {0};
static char input_password[WIFI_MAX_PASSWORD] = {0};
static char input_timezone[48] = {0};
static char input_location[64] = {0};
static char status_msg[128] = {0};
static int msg_timer = 0;
static int selected = 0;
static int scan_count = 0;
static int scan_selected = 0;
static ui_text_input_widget_t *ssid_input;
static ui_text_input_widget_t *password_input;
static ui_text_input_widget_t *timezone_input;
static ui_text_input_widget_t *location_input;

#define SETTINGS_KEY_TIMEZONE "time/timezone"
#define SETTINGS_KEY_LOCATION "weather/location"
#define SETTINGS_KEY_SERIAL_LOG "system/serial_log_output"
#define WEATHER_GEOCODE_URL_FMT "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json"
#define WEATHER_HTTP_TIMEOUT_MS 15000

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

    if (lat_out[0] == '\0' || lon_out[0] == '\0') {
        return 0;
    }

    for (size_t index = 0; lat_out[index] != '\0'; index++) {
        char ch = lat_out[index];
        if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+')) {
            return 0;
        }
    }

    for (size_t index = 0; lon_out[index] != '\0'; index++) {
        char ch = lon_out[index];
        if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+')) {
            return 0;
        }
    }

    return 1;
}

static int extract_json_number_field(const char *json, const char *field_name, char *out, size_t out_size) {
    if (!json || !field_name || !out || out_size == 0) {
        return 0;
    }

    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\":", field_name);
    const char *field = strstr(json, needle);
    if (!field) {
        return 0;
    }

    const char *value = field + strlen(needle);
    while (*value == ' ' || *value == '\t') {
        value++;
    }

    size_t out_len = 0;
    if (*value == '-' || *value == '+') {
        if (out_len + 1 >= out_size) {
            return 0;
        }
        out[out_len++] = *value++;
    }

    while (((*value >= '0' && *value <= '9') || *value == '.') && out_len + 1 < out_size) {
        out[out_len++] = *value++;
    }

    out[out_len] = '\0';
    return out_len > 0;
}

static int resolve_location(const char *location, char *resolved_out, size_t resolved_size) {
    char latitude[24];
    char longitude[24];
    if (parse_location_lat_lon(location, latitude, sizeof(latitude), longitude, sizeof(longitude))) {
        snprintf(resolved_out, resolved_size, "%s,%s", latitude, longitude);
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

    if (!extract_json_number_field(geocode_response, "latitude", latitude, sizeof(latitude)) ||
        !extract_json_number_field(geocode_response, "longitude", longitude, sizeof(longitude))) {
        return 0;
    }

    snprintf(resolved_out, resolved_size, "%s,%s", latitude, longitude);
    return 1;
}

static void set_status(const char *msg) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    msg_timer = 150;
}

#define MENU_ITEMS 8
static const char *menu_labels[MENU_ITEMS] = {
    "Scan for networks",
    "Enter SSID",
    "Enter Password",
    "Save && Connect",
    "Disconnect",
    "Set Timezone",
    "Set Location",
    "Serial logs to UART",
};

static void draw_menu_item(int x, int y, int item_index) {
    uint16_t color = (item_index == selected) ? TEXT_COLOR_GREEN : TEXT_COLOR_WHITE;
    char marker = (item_index == selected) ? '>' : ' ';
    text_mode_printf_at_color(x, y, color, "%c %s", marker, menu_labels[item_index]);
}

static void draw_main(void) {
    ui_clear();

    int y = 0;
    ui_label_attr((TEXT_MODE_COLS - 13) / 2, y++, "  WiFi Setup  ", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui_separator(y++);

    if (wifi_is_connected()) {
        ui_label_attr(3, y++, "Status: Connected", TEXT_COLOR_GREEN, TEXT_ATTR_NORMAL);
        char ip_str[48];
        snprintf(ip_str, sizeof(ip_str), "IP: %s", wifi_get_ip());
        ui_label(3, y++, ip_str, TEXT_COLOR_WHITE);
    } else {
        ui_label_attr(3, y++, "Status: Disconnected", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    }

    ui_label_attr(3,
                  y++,
                  serial_log_output_is_enabled() ? "Serial logs: Enabled" : "Serial logs: Disabled",
                  serial_log_output_is_enabled() ? TEXT_COLOR_YELLOW : TEXT_COLOR_GREEN,
                  TEXT_ATTR_NORMAL);

    char timezone_line[72];
    char location_line[72];
    snprintf(timezone_line, sizeof(timezone_line), "Timezone: %s", input_timezone[0] ? input_timezone : "UTC");
    snprintf(location_line, sizeof(location_line), "Location: %s", input_location[0] ? input_location : "40.4168,-3.7038");
    ui_label_attr(3, y++, timezone_line, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    ui_label_attr(3, y++, location_line, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    ui_separator(y++);

    ui_label_attr(3, y++, "WiFi", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    draw_menu_item(5, y++, 0);
    draw_menu_item(5, y++, 1);
    draw_menu_item(5, y++, 2);
    draw_menu_item(5, y++, 3);
    draw_menu_item(5, y++, 4);

    ui_separator(y++);
    ui_label_attr(3, y++, "Time & Location", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    draw_menu_item(5, y++, 5);
    draw_menu_item(5, y++, 6);

    ui_separator(y++);
    ui_label_attr(3, y++, "Debug", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    draw_menu_item(5, y++, 7);
    y++;

    if (status_msg[0]) {
        ui_label_attr(3, y, status_msg, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
    }

    ui_status_bar(TEXT_MODE_ROWS - 2, "W/S Navigate  Enter Select", "");
}

static void draw_scan_results(void) {
    ui_clear();

    int y = 0;
    ui_label_attr((TEXT_MODE_COLS - 19) / 2, y++, "Available Networks", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui_separator(y++);

    if (scan_count <= 0) {
        ui_label_attr(3, y, "No networks found", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    } else {
        y++;
        if (scan_selected >= scan_count) scan_selected = 0;

        for (int i = 0; i < scan_count && i < TEXT_MODE_ROWS - 6; i++) {
            const char *ssid = wifi_scan_get_ssid(i);
            int rssi = wifi_scan_get_rssi(i);
            int quality = (rssi + 100) * 100 / 70;
            if (quality < 0) quality = 0;
            if (quality > 100) quality = 100;

            uint8_t color = TEXT_COLOR_GREEN;
            if (quality < 40) color = TEXT_COLOR_RED;
            else if (quality < 70) color = TEXT_COLOR_YELLOW;

            char marker = (i == scan_selected) ? '>' : ' ';
            char line[48];
            snprintf(line, sizeof(line), "%c %-24s %3d dBm", marker, ssid, rssi);
            ui_label(3, y + i, line, color);
        }
    }

    ui_status_bar(TEXT_MODE_ROWS - 3, "W/S Navigate  Enter Select", "ESC Back");
}

static void draw_message(void) {
    ui_clear();

    int y = 0;
    ui_label_attr((TEXT_MODE_COLS - 10) / 2, y++, "  Message  ", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui_separator(y++);
    y++;

    ui_label_attr(3, y, status_msg, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
    ui_status_bar(TEXT_MODE_ROWS - 3, "Press any key", "");
}

static void render(void) {
    switch (state) {
        case STATE_MAIN:
            draw_main();
            break;
        case STATE_SCAN_RESULTS:
            draw_scan_results();
            break;
        case STATE_ENTER_SSID:
            ui_text_input_set_buffer(ssid_input, input_ssid, sizeof(input_ssid));
            ui_text_input_draw(ssid_input);
            break;
        case STATE_ENTER_PASSWORD:
            ui_text_input_set_buffer(password_input, input_password, sizeof(input_password));
            ui_text_input_draw(password_input);
            break;
        case STATE_ENTER_TIMEZONE:
            ui_text_input_set_buffer(timezone_input, input_timezone, sizeof(input_timezone));
            ui_text_input_draw(timezone_input);
            break;
        case STATE_ENTER_LOCATION:
            ui_text_input_set_buffer(location_input, input_location, sizeof(input_location));
            ui_text_input_draw(location_input);
            break;
        case STATE_MESSAGE:
            draw_message();
            break;
    }
    text_mode_flush();
}

void app_init(app_context_t *ctx) {
    os_log(TAG, "Settings app initializing");

    if (!text_mode_init()) {
        os_log(TAG, "Failed to init text mode");
        return;
    }

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 100;

    state = STATE_MAIN;
    status_msg[0] = '\0';
    msg_timer = 0;
    input_ssid[0] = '\0';
    input_password[0] = '\0';
    selected = 0;
    scan_selected = 0;

    serial_log_output_set_enabled(os_settings_get_bool(SETTINGS_KEY_SERIAL_LOG, false));

    os_settings_get_string(SETTINGS_KEY_TIMEZONE, "UTC", input_timezone, sizeof(input_timezone));
    os_settings_get_string(SETTINGS_KEY_LOCATION, "40.4168,-3.7038", input_location, sizeof(input_location));

    // Create text input widgets
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    ssid_input = ui_text_input_create(0, rows - 4, cols, 4);
    ui_text_input_set_title(ssid_input, "Enter SSID");
    ui_text_input_set_label(ssid_input, "SSID:");
    ui_text_input_set_hints(ssid_input, "Type to enter  Enter Confirm", "ESC Cancel");

    password_input = ui_text_input_create(0, rows - 4, cols, 4);
    ui_text_input_set_title(password_input, "Enter Password");
    ui_text_input_set_label(password_input, "Password:");
    ui_text_input_set_mask(password_input, true);
    ui_text_input_set_hints(password_input, "Type to enter  Enter Confirm", "ESC Cancel");

    timezone_input = ui_text_input_create(0, rows - 4, cols, 4);
    ui_text_input_set_title(timezone_input, "Set Timezone");
    ui_text_input_set_label(timezone_input, "Timezone:");
    ui_text_input_set_hints(timezone_input, "Ex: UTC or Europe/Madrid", "ESC Cancel");

    location_input = ui_text_input_create(0, rows - 4, cols, 4);
    ui_text_input_set_title(location_input, "Set Location");
    ui_text_input_set_label(location_input, "Location:");
    ui_text_input_set_hints(location_input, "City or lat,lon", "ESC Cancel");

    render();
    os_log(TAG, "Settings app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    text_mode_save();
}

void app_close(app_context_t *ctx) {
    os_log(TAG, "Settings app cleanup");

    // Clean up text input widgets
    if (ssid_input) {
        ui_text_input_destroy(ssid_input);
        ssid_input = NULL;
    }
    if (password_input) {
        ui_text_input_destroy(password_input);
        password_input = NULL;
    }
    if (timezone_input) {
        ui_text_input_destroy(timezone_input);
        timezone_input = NULL;
    }
    if (location_input) {
        ui_text_input_destroy(location_input);
        location_input = NULL;
    }

    text_mode_clear(TEXT_COLOR_BLACK);
}

static void handle_main_key(char key) {
    int old = selected;

    if (key == 'w' || key == 'W') {
        selected = (selected - 1 + MENU_ITEMS) % MENU_ITEMS;
    } else if (key == 's' || key == 'S') {
        selected = (selected + 1) % MENU_ITEMS;
    } else if (key == '\n' || key == '\r') {
        switch (selected) {
            case 0:
                state = STATE_MESSAGE;
                set_status("Scanning for networks...");
                render();
                scan_count = wifi_scan();
                state = STATE_SCAN_RESULTS;
                scan_selected = 0;
                msg_timer = 0;
                status_msg[0] = '\0';
                render();
                break;
            case 1:
                state = STATE_ENTER_SSID;
                input_ssid[0] = '\0';
                render();
                break;
            case 2:
                state = STATE_ENTER_PASSWORD;
                input_password[0] = '\0';
                render();
                break;
            case 3:
                if (input_ssid[0] == '\0') {
                    set_status("Enter SSID first");
                    render();
                    return;
                }
                set_status("Connecting...");
                render();
                wifi_save_config(input_ssid, input_password);
                wifi_connect(input_ssid, input_password);
                msg_timer = 80;
                break;
            case 4:
                wifi_disconnect();
                set_status("Disconnected");
                break;
            case 5: {
                state = STATE_ENTER_TIMEZONE;
                render();
                break;
            }
            case 6: {
                state = STATE_ENTER_LOCATION;
                render();
                break;
            }
            case 7: {
                bool enabled = !serial_log_output_is_enabled();
                serial_log_output_set_enabled(enabled);
                os_settings_set_bool(SETTINGS_KEY_SERIAL_LOG, enabled);
                set_status(enabled ? "Serial log output enabled" : "Serial log output disabled");
                render();
                break;
            }
        }
    } else if (key == 27) {
        return;
    }

    if (selected != old) {
        render();
    }
}

static void handle_scan_key(char key) {
    if (key == 27) {
        state = STATE_MAIN;
        render();
        return;
    }

    if (scan_count <= 0) return;

    int old = scan_selected;

    if (key == 'w' || key == 'W') {
        scan_selected = (scan_selected - 1 + scan_count) % scan_count;
    } else if (key == 's' || key == 'S') {
        scan_selected = (scan_selected + 1) % scan_count;
    } else if (key == '\n' || key == '\r') {
        if (scan_selected >= 0 && scan_selected < scan_count) {
            const char *ssid = wifi_scan_get_ssid(scan_selected);
            if (ssid && ssid[0]) {
                strncpy(input_ssid, ssid, sizeof(input_ssid) - 1);
                state = STATE_ENTER_PASSWORD;
                input_password[0] = '\0';
                render();
            }
        }
    }

    if (scan_selected != old) {
        render();
    }
}

static void handle_text_entry_event(event_t *event) {
    ui_text_input_widget_t *widget = NULL;
    if (state == STATE_ENTER_SSID) {
        widget = ssid_input;
    } else if (state == STATE_ENTER_PASSWORD) {
        widget = password_input;
    } else if (state == STATE_ENTER_TIMEZONE) {
        widget = timezone_input;
    } else if (state == STATE_ENTER_LOCATION) {
        widget = location_input;
    }
    if (!widget) {
        return;
    }

    char key = event->keyboard.key;
    if (ui_text_input_handle_key(widget, key)) {
        ui_text_input_draw(widget);
        text_mode_flush();

        // Handle confirmations based on state
        if (key == '\n' || key == '\r') {
            if (state == STATE_ENTER_TIMEZONE) {
                os_settings_set_string(SETTINGS_KEY_TIMEZONE, input_timezone);
                set_status("Timezone saved");
            } else if (state == STATE_ENTER_LOCATION) {
                trim_spaces(input_location);
                char resolved_location[64];
                if (!resolve_location(input_location, resolved_location, sizeof(resolved_location))) {
                    state = STATE_MESSAGE;
                    set_status("Location lookup failed");
                    render();
                    return;
                }
                snprintf(input_location, sizeof(input_location), "%s", resolved_location);
                os_settings_set_string(SETTINGS_KEY_LOCATION, input_location);
                set_status("Location saved as lat,lon");
            }
            state = STATE_MAIN;
            render();
        } else if (key == 27) { // ESC
            state = STATE_MAIN;
            render();
        }
    }
}

static void handle_message_key(char key) {
    (void)key;
    state = STATE_MAIN;
    render();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        if (event->keyboard.modifiers & MODIFIER_CTRL) {
            return;
        }

        switch (state) {
            case STATE_MAIN:
                handle_main_key(key);
                break;
            case STATE_SCAN_RESULTS:
                handle_scan_key(key);
                break;
            case STATE_ENTER_SSID:
            case STATE_ENTER_PASSWORD:
            case STATE_ENTER_TIMEZONE:
            case STATE_ENTER_LOCATION:
                handle_text_entry_event(event);
                break;
            case STATE_MESSAGE:
                handle_message_key(key);
                break;
        }
    } else if (event->type == EVENT_TIMER) {
        if (msg_timer > 0) {
            msg_timer--;
            if (msg_timer == 0) {
                status_msg[0] = '\0';
                state = STATE_MAIN;
                render();
            }
        }
    }
}
