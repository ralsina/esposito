#include "os_core.h"
#include "text_mode.h"
#include "ui2.h"
#include "ui2_toolbar.h"
#include "lucide_icons.h"
#include "wifi.h"
#include "app_config.h"
#include "hardware.h"
#include "ble_keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static const char *TAG = "settings";

extern const char *ota_firmware_version(void);
extern bool ota_check_for_update(char *latest_version, size_t max_len);
extern const char *ota_apply_update(void);
extern int64_t esp_timer_get_time(void);

typedef enum {
    STATE_MAIN,
    STATE_SCAN_RESULTS,
    STATE_ENTER_PASSWORD,
    STATE_WIFI_CONNECTING,
    STATE_ENTER_TIMEZONE,
    STATE_ENTER_LOCATION,
    STATE_FONT_SELECTION,
    STATE_FONT_SIZE_SELECTION,
} app_state_t;

static app_state_t state = STATE_MAIN;
static ui2_screen_t *screen;
static char input_ssid[64] = {0};
static char input_password[64] = {0};
static char input_timezone[48] = {0};
static char input_location[64] = {0};
static int scan_count = 0;
static int scan_selected = 0;
static char scan_labels[20][48];
static const char *scan_items[20];
static ui2_list_t *scan_list;
static ui2_layout_t *scan_toolbar;
static bool is_ble_scan = false;
static bool ble_scan_pending = false;

static int64_t wifi_connect_start = 0;
static char wifi_connecting_ssid[64] = {0};
static bool wifi_used_stored_password = false;

static ui2_list_t *font_family_list;
static ui2_list_t *font_size_list;
static int font_family_selected = 0;
static int font_size_selected = 0;
static char selected_family[24];

#define MAX_FONTS 64
static char font_family_labels[MAX_FONTS][24];
static const char *font_family_items[MAX_FONTS];
static int font_family_count = 0;
static char font_size_labels[MAX_FONTS][48];
static const char *font_size_items[MAX_FONTS];
static int font_size_count = 0;

typedef enum {
    SECTION_WIFI,
    SECTION_BLUETOOTH,
    SECTION_TIME,
    SECTION_DISPLAY,
    SECTION_DEBUG,
    SECTION_SYSTEM,
    SECTION_COUNT,
} settings_section_t;

typedef enum {
    ACTION_CHOOSE_NETWORK,
    ACTION_IP_INFO,
    ACTION_DISCONNECT,
    ACTION_SET_TIMEZONE,
    ACTION_SET_LOCATION,
    ACTION_SET_FONT_FAMILY,
    ACTION_SET_FONT_SIZE,
    ACTION_SET_ROTATION,
    ACTION_SET_BRIGHTNESS,
    ACTION_SET_SCREENSAVER_TIMEOUT,
    ACTION_SET_PALETTE,
    ACTION_TOGGLE_SERIAL,
    ACTION_CHECK_UPDATE,
    ACTION_APPLY_UPDATE,
    ACTION_BLE_SCAN,
    ACTION_BLE_CONNECT,
    ACTION_BLE_DISCONNECT,
} settings_action_t;

typedef struct {
    const char *label;
    settings_action_t action;
} section_option_t;

static const char *section_labels[SECTION_COUNT] = {
    "WiFi", "BT", "Time", "Display", "Debug", "System",
};

static const section_option_t wifi_connected_opts[] = {
    {"IP", ACTION_IP_INFO},
    {"Disconnect", ACTION_DISCONNECT},
};
static const section_option_t wifi_disconnected_opts[] = {
    {"Choose Network", ACTION_CHOOSE_NETWORK},
};

static const section_option_t ble_options[] = {
    {"Scan", ACTION_BLE_SCAN},
    {"Disconnect", ACTION_BLE_DISCONNECT},
};

static const section_option_t time_options[] = {
    {"Timezone", ACTION_SET_TIMEZONE},
    {"Location", ACTION_SET_LOCATION},
};

static const section_option_t display_options[] = {
    {"Font Family", ACTION_SET_FONT_FAMILY},
    {"Font Size", ACTION_SET_FONT_SIZE},
    {"Rotation", ACTION_SET_ROTATION},
    {"Brightness", ACTION_SET_BRIGHTNESS},
    {"Screensaver", ACTION_SET_SCREENSAVER_TIMEOUT},
    {"Palette", ACTION_SET_PALETTE},
};

static const section_option_t debug_options[] = {
    {"Serial UART", ACTION_TOGGLE_SERIAL},
};

static const section_option_t system_options[] = {
    {"Version", ACTION_CHECK_UPDATE},
    {"Update Firmware", ACTION_APPLY_UPDATE},
};

#define MAX_OPTIONS_PER_SECTION 16

static struct {
    ui2_list_t *list;
    char item_labels[MAX_OPTIONS_PER_SECTION][64];
    const char *item_ptrs[MAX_OPTIONS_PER_SECTION];
    settings_action_t actions[MAX_OPTIONS_PER_SECTION];
    int count;
} tab_content[SECTION_COUNT];

static ui2_tabview_t *tv;
static ui2_layout_t *toolbar;

#define PALETTE_COUNT 4

static const uint16_t palette_cga[16] = {
    0x0000, 0x0010, 0x0400, 0x0410,
    0x8000, 0x8010, 0x8400, 0x8410,
    0x4208, 0x001F, 0x07E0, 0x07FF,
    0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

static const uint16_t palette_cga_light[16] = {
    0xFFBC, 0x0013, 0x0440, 0x0453,
    0x8800, 0x8813, 0x8840, 0x0000,
    0x8410, 0x001F, 0x07E0, 0x07FF,
    0xF800, 0xF81F, 0xFFE0, 0xFFFF,
};

static const uint16_t palette_solarized_dark[16] = {
    0x0146, 0x6B98, 0x84C0, 0x2D13,
    0xD985, 0xD1B0, 0xB440, 0x84B2,
    0x01A8, 0x245A, 0x07E0, 0x63D0,
    0xEF5A, 0xCA42, 0x5B6E, 0x9514,
};

static const uint16_t palette_solarized_light[16] = {
    0xFFBC, 0x6B98, 0x84C0, 0x2D13,
    0xD985, 0xD1B0, 0xB440, 0x63D0,
    0xEF5A, 0x245A, 0xCA42, 0x5B6E,
    0x0146, 0x01A8, 0x01A8, 0x84B2,
};

static const char *palette_names[PALETTE_COUNT] = {
    "CGA", "CGA Light", "Solarized Dark", "Solarized Light",
};

static const uint16_t *palette_data[PALETTE_COUNT] = {
    palette_cga, palette_cga_light, palette_solarized_dark, palette_solarized_light,
};

#define SETTINGS_KEY_TIMEZONE "time/timezone"
#define SETTINGS_KEY_LOCATION "weather/location"
#define SETTINGS_KEY_SERIAL_LOG "system/serial_log_output"
#define SETTINGS_KEY_DEFAULT_FONT "system/default_font"
#define SETTINGS_KEY_SCREEN_ROTATION "display/rotation"
#define WEATHER_GEOCODE_URL_FMT "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json"
#define WEATHER_HTTP_TIMEOUT_MS 15000

static void trim_spaces(char *text) {
    if (!text || !text[0]) return;
    size_t start = 0;
    while (text[start] == ' ' || text[start] == '\t') start++;
    if (start > 0) memmove(text, text + start, strlen(text + start) + 1);
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len--;
    }
}

static void url_encode_basic(const char *src, char *dst, size_t dst_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;
    if (!src || !dst || dst_size == 0) return;
    for (size_t index = 0; src[index] != '\0' && out + 1 < dst_size; index++) {
        unsigned char ch = (unsigned char)src[index];
        bool safe = (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            dst[out++] = (char)ch;
        } else {
            if (out + 3 >= dst_size) break;
            dst[out++] = '%';
            dst[out++] = hex[(ch >> 4) & 0x0F];
            dst[out++] = hex[ch & 0x0F];
        }
    }
    dst[out] = '\0';
}

static int parse_location_lat_lon(const char *location, char *lat_out, size_t lat_size, char *lon_out, size_t lon_size) {
    if (!location || !lat_out || !lon_out || lat_size == 0 || lon_size == 0) return 0;
    const char *comma = strchr(location, ',');
    if (!comma) return 0;
    size_t lat_len = (size_t)(comma - location);
    size_t lon_len = strlen(comma + 1);
    if (lat_len == 0 || lon_len == 0 || lat_len >= lat_size || lon_len >= lon_size) return 0;
    memcpy(lat_out, location, lat_len);
    lat_out[lat_len] = '\0';
    memcpy(lon_out, comma + 1, lon_len);
    lon_out[lon_len] = '\0';
    trim_spaces(lat_out);
    trim_spaces(lon_out);
    if (lat_out[0] == '\0' || lon_out[0] == '\0') return 0;
    for (size_t index = 0; lat_out[index] != '\0'; index++) {
        char ch = lat_out[index];
        if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+')) return 0;
    }
    for (size_t index = 0; lon_out[index] != '\0'; index++) {
        char ch = lon_out[index];
        if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+')) return 0;
    }
    return 1;
}

static int extract_json_number_field(const char *json, const char *field_name, char *out, size_t out_size) {
    if (!json || !field_name || !out || out_size == 0) return 0;
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\":", field_name);
    const char *field = strstr(json, needle);
    if (!field) return 0;
    const char *value = field + strlen(needle);
    while (*value == ' ' || *value == '\t') value++;
    size_t out_len = 0;
    if (*value == '-' || *value == '+') {
        if (out_len + 1 >= out_size) return 0;
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
    if (encoded[0] == '\0') return 0;
    snprintf(geocode_url, sizeof(geocode_url), WEATHER_GEOCODE_URL_FMT, encoded);
    int geocode_result = os_http_get(geocode_url, geocode_response, sizeof(geocode_response), WEATHER_HTTP_TIMEOUT_MS);
    if (geocode_result <= 0) return 0;
    if (!extract_json_number_field(geocode_response, "latitude", latitude, sizeof(latitude)) ||
        !extract_json_number_field(geocode_response, "longitude", longitude, sizeof(longitude)))
        return 0;
    snprintf(resolved_out, resolved_size, "%s,%s", latitude, longitude);
    return 1;
}

static void truncate_text(const char *text, char *out, size_t out_size, int max_chars) {
    if (!text || !out || out_size == 0) return;
    if (max_chars <= 0) { out[0] = '\0'; return; }
    size_t source_len = strlen(text);
    int usable = max_chars;
    if (usable > (int)out_size - 1) usable = (int)out_size - 1;
    if ((int)source_len <= usable) { snprintf(out, out_size, "%s", text); return; }
    if (usable == 1) { out[0] = '~'; out[1] = '\0'; return; }
    memcpy(out, text, (size_t)(usable - 1));
    out[usable - 1] = '~';
    out[usable] = '\0';
}

static const section_option_t *section_options(settings_section_t section, int *count_out) {
    if (count_out) *count_out = 0;
    switch (section) {
        case SECTION_WIFI:
            if (wifi_is_connected()) {
                if (count_out) *count_out = (int)(sizeof(wifi_connected_opts) / sizeof(wifi_connected_opts[0]));
                return wifi_connected_opts;
            } else {
                if (count_out) *count_out = (int)(sizeof(wifi_disconnected_opts) / sizeof(wifi_disconnected_opts[0]));
                return wifi_disconnected_opts;
            }
        case SECTION_BLUETOOTH: if (count_out) *count_out = (int)(sizeof(ble_options) / sizeof(ble_options[0])); return ble_options;
        case SECTION_TIME: if (count_out) *count_out = (int)(sizeof(time_options) / sizeof(time_options[0])); return time_options;
        case SECTION_DISPLAY: if (count_out) *count_out = (int)(sizeof(display_options) / sizeof(display_options[0])); return display_options;
        case SECTION_DEBUG: if (count_out) *count_out = (int)(sizeof(debug_options) / sizeof(debug_options[0])); return debug_options;
        case SECTION_SYSTEM: if (count_out) *count_out = (int)(sizeof(system_options) / sizeof(system_options[0])); return system_options;
        default: return NULL;
    }
}

static void format_action_value(settings_action_t action, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    switch (action) {
        case ACTION_CHOOSE_NETWORK:
            out[0] = '\0';
            break;
        case ACTION_IP_INFO: {
            const char *ip = wifi_get_ip();
            if (ip && ip[0])
                snprintf(out, out_size, "%s", ip);
            else
                snprintf(out, out_size, "%s", "\xe2\x80\x94");
            break;
        }
        case ACTION_DISCONNECT: {
            char ssid[64];
            os_settings_get_string("wifi/last_ssid", "", ssid, sizeof(ssid));
            if (ssid[0])
                snprintf(out, out_size, "%s", ssid);
            else
                snprintf(out, out_size, "%s", "connected");
            break;
        }
        case ACTION_BLE_SCAN:
            if (ble_keyboard_is_initializing()) {
                snprintf(out, out_size, "%s", "init...");
            } else {
                snprintf(out, out_size, "%s", ble_keyboard_is_available() ? "ready" : "off");
            }
            break;
        case ACTION_BLE_CONNECT:
            snprintf(out, out_size, "%s", ble_keyboard_is_connected() ? ble_keyboard_get_connected_name() : "none");
            break;
        case ACTION_BLE_DISCONNECT:
            snprintf(out, out_size, "%s", ble_keyboard_is_connected() ? "connected" : "\xe2\x80\x94");
            break;
        case ACTION_SET_TIMEZONE:
            snprintf(out, out_size, "%s", input_timezone[0] ? input_timezone : "UTC");
            break;
        case ACTION_SET_LOCATION:
            snprintf(out, out_size, "%s", input_location[0] ? input_location : "40.4168,-3.7038");
            break;
        case ACTION_SET_FONT_FAMILY: {
            char current_font[32];
            os_settings_get_string(SETTINGS_KEY_DEFAULT_FONT, "hack 8", current_font, sizeof(current_font));
            font_id_t current_id = font_lookup_by_name(current_font);
            snprintf(out, out_size, "%s", current_id >= 0 ? font_table[current_id].family : "hack");
            break;
        }
        case ACTION_SET_FONT_SIZE: {
            char current_font[32];
            os_settings_get_string(SETTINGS_KEY_DEFAULT_FONT, "hack 8", current_font, sizeof(current_font));
            font_id_t current_id = font_lookup_by_name(current_font);
            snprintf(out, out_size, "%d", current_id >= 0 ? font_table[current_id].size : 8);
            break;
        }
        case ACTION_SET_ROTATION: {
            int current = os_settings_get_int(SETTINGS_KEY_SCREEN_ROTATION, 1);
            const char *rot_names[] = {"0\xc2\xb0", "90\xc2\xb0", "180\xc2\xb0", "270\xc2\xb0"};
            snprintf(out, out_size, "%s", rot_names[current]);
            break;
        }
        case ACTION_SET_BRIGHTNESS: {
            int current = os_settings_get_int("display/backlight", 255);
            snprintf(out, out_size, "%d%%", current * 100 / 255);
            break;
        }
        case ACTION_SET_SCREENSAVER_TIMEOUT: {
            int timeout = os_settings_get_int("system/screensaver_timeout", 5);
            snprintf(out, out_size, "%s", timeout > 0 ? "5 min" : "off");
            break;
        }
        case ACTION_SET_PALETTE: {
            int index = os_settings_get_int("display/palette", 0);
            snprintf(out, out_size, "%s", (index >= 0 && index < PALETTE_COUNT) ? palette_names[index] : "CGA");
            break;
        }
        case ACTION_TOGGLE_SERIAL:
            snprintf(out, out_size, "%s", serial_log_output_is_enabled() ? "on" : "off");
            break;
        case ACTION_CHECK_UPDATE:
            snprintf(out, out_size, "%s", ota_firmware_version());
            break;
        case ACTION_APPLY_UPDATE:
            out[0] = '\0';
            break;
    }
}

static void build_font_size_items(const char *family);
static void setup_and_render(void);
static void on_scan_activated(int item_index, void *user_data);
static void on_scan_selection_changed(int item_index, void *user_data);

static void force_render(void) {
    if (ui2_osk_is_active()) return;
    text_mode_clear(TEXT_COLOR_BLACK);
    ui2_screen_render(screen);
}

static void on_exit_btn_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    os_exit();
}

static void on_toolbar_up(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    UI2_WIDGET(tv)->vtable->handle_key(UI2_WIDGET(tv), 'w');
    force_render();
}

static void on_toolbar_down(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    UI2_WIDGET(tv)->vtable->handle_key(UI2_WIDGET(tv), 's');
    force_render();
}

static void on_toolbar_activate(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    UI2_WIDGET(tv)->vtable->handle_key(UI2_WIDGET(tv), '\n');
    force_render();
}

static void on_toolbar_back(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    UI2_WIDGET(tv)->vtable->handle_key(UI2_WIDGET(tv), 'a');
    force_render();
}

static void on_scan_toolbar_up(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (scan_list) {
        UI2_WIDGET(scan_list)->vtable->handle_key(UI2_WIDGET(scan_list), 'w');
        scan_selected = ui2_list_get_selection(scan_list);
        force_render();
    }
}

static void on_scan_toolbar_down(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (scan_list) {
        UI2_WIDGET(scan_list)->vtable->handle_key(UI2_WIDGET(scan_list), 's');
        scan_selected = ui2_list_get_selection(scan_list);
        force_render();
    }
}

static void on_scan_toolbar_sel(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (scan_list && scan_count > 0) {
        on_scan_activated(scan_selected, NULL);
    }
}

static void on_scan_toolbar_connect(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (scan_selected < 0 || scan_selected >= scan_count) return;

    bool sel_is_connected = ble_keyboard_is_connected() &&
                            ble_keyboard_get_connected_name()[0] != '\0' &&
                            strcmp(ble_keyboard_get_scan_name(scan_selected),
                                   ble_keyboard_get_connected_name()) == 0;
    if (sel_is_connected) {
        ble_keyboard_disconnect();
        ui2_screen_toast_show(screen, "Disconnected", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
    } else {
        ui2_screen_toast_show(screen, "Connecting...", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
        force_render();
        text_mode_flush();
        if (ble_keyboard_connect(scan_selected)) {
            ui2_screen_toast_show(screen, "Connected", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
        } else {
            ui2_screen_toast_show(screen, "Connection failed", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
        }
    }
    force_render();
}

static void on_scan_toolbar_back(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    state = STATE_MAIN;
    setup_and_render();
}

static void wifi_make_pass_key(char *out, size_t out_size, const char *ssid) {
    char sanitized[64] = {0};
    size_t j = 0;
    for (size_t i = 0; ssid[i] && j < sizeof(sanitized) - 1; i++) {
        char c = ssid[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            sanitized[j++] = c;
        } else {
            sanitized[j++] = '_';
        }
    }
    snprintf(out, out_size, "wifi/pass_%s", sanitized);
}

static bool wifi_get_stored_password(const char *ssid, char *out, size_t out_size) {
    char key[80];
    wifi_make_pass_key(key, sizeof(key), ssid);
    size_t len = os_settings_get_string(key, "", out, out_size);
    return len > 0 && out[0];
}

static void wifi_store_password(const char *ssid, const char *password) {
    char key[80];
    wifi_make_pass_key(key, sizeof(key), ssid);
    os_settings_set_string(key, password);
}

static void start_wifi_connect(const char *ssid, const char *password, bool from_storage) {
    snprintf(wifi_connecting_ssid, sizeof(wifi_connecting_ssid), "%s", ssid);
    wifi_used_stored_password = from_storage;
    wifi_connect_start = esp_timer_get_time();

    wifi_save_config(ssid, password);
    wifi_connect(ssid, password);
    os_settings_set_string("wifi/last_ssid", ssid);
    wifi_store_password(ssid, password);

    ui2_screen_toast_show(screen, "Connecting...", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 150);
    state = STATE_WIFI_CONNECTING;
}

static void execute_main_action(settings_action_t action) {
    switch (action) {
        case ACTION_CHOOSE_NETWORK: {
            is_ble_scan = false;
            wifi_init();
            ui2_screen_toast_show(screen, "Scanning...", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 150);
            force_render();
            text_mode_flush();
            scan_count = wifi_scan();
            scan_selected = 0;
            for (int i = 0; i < scan_count && i < 20; i++) {
                const char *ssid = wifi_scan_get_ssid(i);
                int rssi = wifi_scan_get_rssi(i);
                int quality = (rssi + 100) * 100 / 70;
                if (quality < 0) quality = 0;
                if (quality > 100) quality = 100;
                const char *ql = quality >= 70 ? "Good" : quality >= 40 ? "Fair" : "Weak";
                snprintf(scan_labels[i], sizeof(scan_labels[i]), "%-24s %3ddBm [%s]", ssid, rssi, ql);
                scan_items[i] = scan_labels[i];
            }
            state = STATE_SCAN_RESULTS;
            break;
        }
        case ACTION_DISCONNECT:
            wifi_disconnect();
            ui2_screen_toast_show(screen, "Disconnected", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
        case ACTION_SET_TIMEZONE:
            state = STATE_ENTER_TIMEZONE;
            if (!keyboard_is_available())
                ui2_osk_input_text("Set Timezone", input_timezone, sizeof(input_timezone), input_timezone, false);
            setup_and_render();
            break;
        case ACTION_SET_LOCATION:
            state = STATE_ENTER_LOCATION;
            if (!keyboard_is_available())
                ui2_osk_input_text("Set Location", input_location, sizeof(input_location), input_location, false);
            setup_and_render();
            break;
        case ACTION_SET_FONT_FAMILY:
            state = STATE_FONT_SELECTION;
            setup_and_render();
            break;
        case ACTION_SET_FONT_SIZE: {
            build_font_size_items(selected_family);
            font_size_selected = 0;
            char current_font[32];
            os_settings_get_string(SETTINGS_KEY_DEFAULT_FONT, "hack 8", current_font, sizeof(current_font));
            font_id_t current_id = font_lookup_by_name(current_font);
            if (current_id >= 0) {
                for (int i = 0; i < font_size_count; i++) {
                    int size = 0;
                    sscanf(font_size_items[i], "%d", &size);
                    if (size == font_table[current_id].size) {
                        font_size_selected = i;
                        break;
                    }
                }
            }
            if (font_size_list) {
                ui2_list_set_items(font_size_list, font_size_items, font_size_count);
                ui2_list_set_selection(font_size_list, font_size_selected);
            }
            state = STATE_FONT_SIZE_SELECTION;
            setup_and_render();
            break;
        }
        case ACTION_SET_ROTATION: {
            int current = os_settings_get_int(SETTINGS_KEY_SCREEN_ROTATION, 1);
            int new_rotation = (current + 1) % 4;
            os_settings_set_int(SETTINGS_KEY_SCREEN_ROTATION, new_rotation);
            display_set_rotation(new_rotation);
            const char *rot_names[] = {"0\xc2\xb0 (Portrait)", "90\xc2\xb0 (Landscape)", "180\xc2\xb0 (Inverted Portrait)", "270\xc2\xb0 (Inverted Landscape)"};
            char msg[64];
            snprintf(msg, sizeof(msg), "Rotation: %s", rot_names[new_rotation]);
            ui2_screen_toast_show(screen, msg, TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
        }
        case ACTION_SET_BRIGHTNESS: {
            static const int levels[] = {255, 192, 128, 64};
            int current = os_settings_get_int("display/backlight", 255);
            int next = levels[0];
            for (int i = 0; i < 3; i++) {
                if (levels[i] == current) { next = levels[i + 1]; break; }
            }
            os_settings_set_int("display/backlight", next);
            display_set_backlight((uint8_t)next);
            char msg[48];
            snprintf(msg, sizeof(msg), "Brightness: %d%%", next * 100 / 255);
            ui2_screen_toast_show(screen, msg, TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
        }
        case ACTION_SET_SCREENSAVER_TIMEOUT: {
            static const int levels[] = {0, 1, 5, 10, 15, 30};
            int current = os_settings_get_int("system/screensaver_timeout", 5);
            int next = levels[0];
            for (int i = 0; i < 5; i++) {
                if (levels[i] == current) { next = levels[i + 1]; break; }
            }
            os_settings_set_int("system/screensaver_timeout", next);
            char msg[48];
            if (next > 0)
                snprintf(msg, sizeof(msg), "Screensaver: %d min", next);
            else
                snprintf(msg, sizeof(msg), "Screensaver: off");
            ui2_screen_toast_show(screen, msg, TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
        }
        case ACTION_SET_PALETTE: {
            int current = os_settings_get_int("display/palette", 0);
            int next = (current + 1) % PALETTE_COUNT;
            os_settings_set_int("display/palette", next);
            text_mode_set_palette(palette_data[next]);
            char msg[48];
            snprintf(msg, sizeof(msg), "Palette: %s", palette_names[next]);
            ui2_screen_toast_show(screen, msg, TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
        }
        case ACTION_TOGGLE_SERIAL: {
            bool enabled = !serial_log_output_is_enabled();
            serial_log_output_set_enabled(enabled);
            os_settings_set_bool(SETTINGS_KEY_SERIAL_LOG, enabled);
            ui2_screen_toast_show(screen, enabled ? "Serial log output enabled" : "Serial log output disabled",
                                  TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
        }
        case ACTION_CHECK_UPDATE: {
            ui2_screen_toast_show(screen, "Checking for update...", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 150);
            force_render();
            char latest[64];
            bool has_update = ota_check_for_update(latest, sizeof(latest));
            ui2_screen_toast_show(screen, has_update ? "Update available" : "Already up to date",
                                  TEXT_COLOR_BLACK, has_update ? TEXT_COLOR_GREEN : TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
        }
        case ACTION_APPLY_UPDATE: {
            if (!wifi_is_connected()) {
                ui2_screen_toast_show(screen, "WiFi not connected!", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
                setup_and_render();
                break;
            }
            ui2_screen_toast_show(screen, "Downloading and flashing...", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 150);
            force_render();
            text_mode_flush();
            const char *err = ota_apply_update();
            if (err) {
                ui2_screen_toast_show(screen, err, TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
                setup_and_render();
            }
            break;
        }
        case ACTION_BLE_SCAN: {
            is_ble_scan = true;
            if (!ble_keyboard_is_available()) {
                if (ble_keyboard_is_initializing()) {
                    ui2_screen_toast_show(screen, "BLE still initializing, try again...", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
                } else {
                    ui2_screen_toast_show(screen, "BLE not available on this device", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
                }
                setup_and_render();
                break;
            }

            scan_count = 0;
            scan_selected = 0;

            state = STATE_SCAN_RESULTS;
            ble_scan_pending = true;
            break;
        }
        case ACTION_BLE_DISCONNECT:
            ble_keyboard_disconnect();
            ui2_screen_toast_show(screen, "BLE keyboard disconnected", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            setup_and_render();
            break;
    }
}

static void build_font_family_items(void) {
    font_family_count = 0;
    for (int index = 0; index < font_count; index++) {
        const char *family = font_table[index].family;
        int found = 0;
        for (int j = 0; j < font_family_count; j++) {
            if (strcmp(font_family_labels[j], family) == 0) { found = 1; break; }
        }
        if (!found) {
            strncpy(font_family_labels[font_family_count], family, sizeof(font_family_labels[0]) - 1);
            font_family_labels[font_family_count][sizeof(font_family_labels[0]) - 1] = '\0';
            font_family_items[font_family_count] = font_family_labels[font_family_count];
            font_family_count++;
        }
    }
}

static int font_size_compare(const void *a, const void *b) {
    int sa = 0, sb = 0;
    sscanf(*(const char**)a, "%d", &sa);
    sscanf(*(const char**)b, "%d", &sb);
    return sa - sb;
}

static void build_font_size_items(const char *family) {
    font_size_count = 0;
    for (int index = 0; index < font_count; index++) {
        if (strcmp(font_table[index].family, family) == 0) {
            int cols = display_get_width() / font_table[index].char_width;
            int rows = display_get_height() / font_table[index].char_height;
            snprintf(font_size_labels[font_size_count], sizeof(font_size_labels[0]),
                     "%d (%dx%d)", font_table[index].size, cols, rows);
            font_size_items[font_size_count] = font_size_labels[font_size_count];
            font_size_count++;
        }
    }
    qsort(font_size_items, font_size_count, sizeof(char*), font_size_compare);
}

static font_id_t find_font_by_family_size(const char *family, int size) {
    for (int index = 0; index < font_count; index++) {
        if (strcmp(font_table[index].family, family) == 0 && font_table[index].size == size)
            return font_table[index].id;
    }
    return FONT_BOOT;
}

static void on_section_item_activated(int item_index, void *user_data) {
    settings_section_t section = (settings_section_t)(intptr_t)user_data;
    int count;
    const section_option_t *options = section_options(section, &count);
    if (item_index >= 0 && item_index < count && options)
        execute_main_action(options[item_index].action);
}

static void build_tab_content(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int content_width = cols - 10 - 1;
    int list_height = rows - 5;

    for (settings_section_t s = 0; s < SECTION_COUNT; s++) {
        int count;
        const section_option_t *options = section_options(s, &count);
        tab_content[s].count = count;
        for (int i = 0; i < count; i++) {
            tab_content[s].actions[i] = options[i].action;
            char value[32];
            format_action_value(options[i].action, value, sizeof(value));
            if (value[0])
                snprintf(tab_content[s].item_labels[i], sizeof(tab_content[s].item_labels[0]),
                         "%s: %s", options[i].label, value);
            else
                snprintf(tab_content[s].item_labels[i], sizeof(tab_content[s].item_labels[0]),
                         "%s", options[i].label);
            tab_content[s].item_ptrs[i] = tab_content[s].item_labels[i];
        }

        ui2_list_t *list = tab_content[s].list;
        if (!list) {
            list = ui2_list_create(0, 0, content_width, list_height);
            ui2_list_set_colors(list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                                TEXT_COLOR_BLACK, TEXT_COLOR_BRIGHT_GREEN, TEXT_COLOR_CYAN);
            ui2_list_set_border(list, true);
            ui2_list_set_scrollbar_width(list, 1);
            tab_content[s].list = list;
            ui2_list_set_callbacks(list, NULL, on_section_item_activated, (void*)(intptr_t)s);
            ui2_layout_add(ui2_tabview_get_content(tv, s), UI2_WIDGET(list));
        }
        ui2_list_set_items(list, tab_content[s].item_ptrs, count);
    }
}

static void on_scan_selection_changed(int item_index, void *user_data) {
    (void)user_data;
    scan_selected = item_index;
}

static void on_scan_activated(int item_index, void *user_data) {
    (void)user_data;
    if (item_index < 0 || item_index >= scan_count) return;

    if (is_ble_scan) {
        scan_selected = item_index;
        force_render();
        return;
    }

    const char *ssid = wifi_scan_get_ssid(item_index);
    if (!ssid || !ssid[0]) return;

    strncpy(input_ssid, ssid, sizeof(input_ssid) - 1);
    input_ssid[sizeof(input_ssid) - 1] = '\0';

    char stored_pass[64];
    if (wifi_get_stored_password(ssid, stored_pass, sizeof(stored_pass))) {
        start_wifi_connect(ssid, stored_pass, true);
    } else {
        state = STATE_ENTER_PASSWORD;
        input_password[0] = '\0';
        if (!keyboard_is_available())
            ui2_osk_input_text("WiFi Password", input_password, sizeof(input_password), NULL, true);
    }
}

static ui2_text_input_t *get_text_input_for_state(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    switch (state) {
        case STATE_ENTER_PASSWORD: {
            ui2_text_input_t *input = ui2_text_input_create(0, rows - 4, cols, 4);
            ui2_text_input_set_title(input, "Enter Password");
            ui2_text_input_set_label(input, "Password:");
            ui2_text_input_set_mask(input, true);
            ui2_text_input_set_hints(input, "Type to enter  Enter Confirm", "ESC Cancel");
            ui2_text_input_set_buffer(input, input_password, sizeof(input_password));
            return input;
        }
        case STATE_ENTER_TIMEZONE: {
            ui2_text_input_t *input = ui2_text_input_create(0, rows - 4, cols, 4);
            ui2_text_input_set_title(input, "Set Timezone");
            ui2_text_input_set_label(input, "Timezone:");
            ui2_text_input_set_hints(input, "Ex: UTC or Europe/Madrid", "ESC Cancel");
            for (int c = 0; input_timezone[c]; c++) {
                if (input_timezone[c] == '/') {
                    ui2_text_input_set_buffer(input, input_timezone, sizeof(input_timezone));
                    return input;
                }
            }
            ui2_text_input_set_buffer(input, input_timezone, sizeof(input_timezone));
            return input;
        }
        case STATE_ENTER_LOCATION: {
            ui2_text_input_t *input = ui2_text_input_create(0, rows - 4, cols, 4);
            ui2_text_input_set_title(input, "Set Location");
            ui2_text_input_set_label(input, "Location:");
            ui2_text_input_set_hints(input, "City or lat,lon", "ESC Cancel");
            ui2_text_input_set_buffer(input, input_location, sizeof(input_location));
            return input;
        }
        default:
            return NULL;
    }
}

static void handle_text_input_confirm(void) {
    if (state == STATE_ENTER_PASSWORD) {
        start_wifi_connect(input_ssid, input_password, false);
        return;
    }
    if (state == STATE_ENTER_TIMEZONE) {
        os_settings_set_string(SETTINGS_KEY_TIMEZONE, input_timezone);
        ui2_screen_toast_show(screen, "Timezone saved", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
    } else if (state == STATE_ENTER_LOCATION) {
        trim_spaces(input_location);
        char resolved_location[64];
        if (!resolve_location(input_location, resolved_location, sizeof(resolved_location))) {
            ui2_screen_toast_show(screen, "Location lookup failed", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
            state = STATE_MAIN;
            setup_and_render();
            return;
        }
        snprintf(input_location, sizeof(input_location), "%s", resolved_location);
        os_settings_set_string(SETTINGS_KEY_LOCATION, input_location);
        ui2_screen_toast_show(screen, "Location saved", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
    }
    state = STATE_MAIN;
}

static void setup_main_view(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    tv = (ui2_tabview_t *)ui2_tabview_create(0, 1, cols, rows - 5, 10);
    UI2_WIDGET(tv)->focusable = true;

    for (int s = 0; s < SECTION_COUNT; s++) {
        ui2_tabview_add_tab(tv, section_labels[s], UI2_LAYOUT_VERTICAL);
        tab_content[s].list = NULL;
        tab_content[s].count = 0;
    }

    build_tab_content();

    {
        ui2_toolbar_item_t tb_items[] = {
            {ICON_ARROW_UP,   on_toolbar_up,       NULL},
            {ICON_ARROW_DOWN, on_toolbar_down,     NULL},
            {ICON_CHECK,      on_toolbar_activate, NULL},
            {ICON_ARROW_LEFT, on_toolbar_back,     NULL},
            {ICON_X,          on_exit_btn_click,   NULL},
        };
        toolbar = ui2_toolbar_create(0, rows - 4, cols, 3, tb_items, 5);
    }

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_layout_add(root, UI2_WIDGET(tv));
    ui2_layout_add(root, UI2_WIDGET(toolbar));

    ui2_screen_set_root(screen, root);
    ui2_screen_focus_set(screen, UI2_WIDGET(tv));
}

static void setup_scan_view(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    if (is_ble_scan) {
        scan_list = ui2_list_create(1, 1, cols - 2, rows - 6);
        ui2_list_set_title(scan_list, "BLE Keyboards");
        ui2_list_set_colors(scan_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                            TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui2_list_set_border(scan_list, true);
        ui2_list_set_scrollbar_width(scan_list, 1);
        ui2_list_set_callbacks(scan_list, on_scan_selection_changed, on_scan_activated, NULL);
        ui2_list_set_items(scan_list, scan_items, scan_count);

        ui2_toolbar_item_t items[] = {
            {ICON_ARROW_UP,   on_scan_toolbar_up,      NULL},
            {ICON_ARROW_DOWN, on_scan_toolbar_down,    NULL},
            {ICON_CHECK,      on_scan_toolbar_connect, NULL},
            {ICON_X,          on_scan_toolbar_back,    NULL},
        };
        scan_toolbar = ui2_toolbar_create(0, rows - 4, cols, 3, items, 4);
    } else {
        scan_list = ui2_list_create(1, 1, cols - 2, rows - 6);
        ui2_list_set_title(scan_list, "Available Networks");
        ui2_list_set_colors(scan_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                            TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui2_list_set_border(scan_list, true);
        ui2_list_set_scrollbar_width(scan_list, 1);
        ui2_list_set_callbacks(scan_list, NULL, on_scan_activated, NULL);
        if (scan_count > 0) {
            ui2_list_set_items(scan_list, scan_items, scan_count);
            ui2_list_set_selection(scan_list, scan_selected);
        }

        ui2_toolbar_item_t items[] = {
            {ICON_ARROW_UP,   on_scan_toolbar_up,   NULL},
            {ICON_ARROW_DOWN, on_scan_toolbar_down, NULL},
            {ICON_CHECK,      on_scan_toolbar_sel,  NULL},
            {ICON_X,          on_scan_toolbar_back, NULL},
        };
        scan_toolbar = ui2_toolbar_create(0, rows - 4, cols, 3, items, 4);
    }

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);

    if (scan_list) {
        ui2_layout_add(root, UI2_WIDGET(scan_list));
    }

    if (scan_toolbar) {
        ui2_layout_add(root, UI2_WIDGET(scan_toolbar));
    }

    if (scan_count <= 0 && !is_ble_scan) {
        ui2_layout_t *msg_layout = ui2_layout_create(0, 0, cols, 7, UI2_LAYOUT_VERTICAL);
        ui2_label_t *msg = ui2_label_create(0, 0, "No networks found",
                                             TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
        ui2_label_t *hint = ui2_label_create(0, 0, "Press ESC to continue",
                                              TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
        ui2_layout_add(msg_layout, UI2_WIDGET(msg));
        ui2_layout_add(msg_layout, UI2_WIDGET(hint));
        ui2_layout_add(root, UI2_WIDGET(msg_layout));
    }

    ui2_screen_set_root(screen, root);
    if (scan_list) {
        ui2_screen_focus_set(screen, UI2_WIDGET(scan_list));
    }
}

static void setup_text_input_view(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    ui2_text_input_t *input = get_text_input_for_state();
    if (!input) return;

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_layout_add(root, UI2_WIDGET(input));

    ui2_screen_set_root(screen, root);
    ui2_screen_focus_set(screen, UI2_WIDGET(input));
}

static void setup_font_family_view(void) {
    int cols = text_mode_get_cols();

    font_family_list = ui2_list_create(2, 2, cols - 4, text_mode_get_rows() - 5);
    ui2_list_set_title(font_family_list, "Select Font Family");
    ui2_list_set_border(font_family_list, true);
    ui2_list_set_scrollbar_width(font_family_list, 1);
    ui2_list_set_items(font_family_list, font_family_items, font_family_count);
    ui2_list_set_selection(font_family_list, font_family_selected);

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, text_mode_get_rows(), UI2_LAYOUT_ABSOLUTE);
    ui2_layout_add(root, UI2_WIDGET(font_family_list));

    ui2_screen_set_root(screen, root);
    ui2_screen_focus_set(screen, UI2_WIDGET(font_family_list));
}

static void setup_font_size_view(void) {
    int cols = text_mode_get_cols();

    font_size_list = ui2_list_create(2, 2, cols - 4, text_mode_get_rows() - 5);
    ui2_list_set_title(font_size_list, "Select Font Size");
    ui2_list_set_border(font_size_list, true);
    ui2_list_set_scrollbar_width(font_size_list, 1);
    ui2_list_set_items(font_size_list, font_size_items, font_size_count);
    ui2_list_set_selection(font_size_list, font_size_selected);

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, text_mode_get_rows(), UI2_LAYOUT_ABSOLUTE);
    ui2_layout_add(root, UI2_WIDGET(font_size_list));

    ui2_screen_set_root(screen, root);
    ui2_screen_focus_set(screen, UI2_WIDGET(font_size_list));
}

static void handle_font_family_activated(int item_index, void *user_data) {
    (void)user_data;
    if (item_index < 0 || item_index >= font_family_count) return;

    const char *new_family = font_family_items[item_index];
    strncpy(selected_family, new_family, sizeof(selected_family) - 1);
    selected_family[sizeof(selected_family) - 1] = '\0';
    char current_font[32];
    os_settings_get_string(SETTINGS_KEY_DEFAULT_FONT, "hack 8", current_font, sizeof(current_font));
    font_id_t current_id = font_lookup_by_name(current_font);
    int current_size = current_id >= 0 ? font_table[current_id].size : 8;

    build_font_size_items(selected_family);
    int best_size = 0;
    font_size_selected = 0;
    for (int i = 0; i < font_size_count; i++) {
        int size = 0;
        sscanf(font_size_items[i], "%d", &size);
        if (best_size == 0 || abs(size - current_size) < abs(best_size - current_size)) {
            best_size = size;
            font_size_selected = i;
        }
    }
    if (font_size_count > 0) {
        int size = 0;
        sscanf(font_size_items[font_size_selected], "%d", &size);
        font_id_t font_id = find_font_by_family_size(selected_family, size);
        if (font_id >= 0 && font_id < font_count) {
            os_settings_set_string(SETTINGS_KEY_DEFAULT_FONT, font_table[font_id].name);
            extern bool text_mode_set_font(font_id_t font);
            text_mode_set_font(font_id);
            ui2_screen_toast_show(screen, "Font changed and saved", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
        }
    }
    state = STATE_MAIN;
    setup_and_render();
}

static void handle_font_size_activated(int item_index, void *user_data) {
    (void)user_data;
    if (item_index < 0 || item_index >= font_size_count) return;

    int size = 0;
    sscanf(font_size_items[item_index], "%d", &size);
    font_id_t font_id = find_font_by_family_size(selected_family, size);
    if (font_id >= 0 && font_id < font_count) {
        os_settings_set_string(SETTINGS_KEY_DEFAULT_FONT, font_table[font_id].name);
        extern bool text_mode_set_font(font_id_t font);
        text_mode_set_font(font_id);
        ui2_screen_toast_show(screen, "Font changed and saved", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
    }
    state = STATE_MAIN;
    setup_and_render();
}

static void setup_and_render(void) {
    if (ui2_osk_is_active()) return;

    switch (state) {
        case STATE_MAIN:
            setup_main_view();
            break;
        case STATE_SCAN_RESULTS:
            setup_scan_view();
            break;
        case STATE_WIFI_CONNECTING:
            setup_main_view();
            break;
        case STATE_ENTER_PASSWORD:
        case STATE_ENTER_TIMEZONE:
        case STATE_ENTER_LOCATION:
            setup_text_input_view();
            break;
        case STATE_FONT_SELECTION:
            setup_font_family_view();
            break;
        case STATE_FONT_SIZE_SELECTION:
            setup_font_size_view();
            break;
    }
    ui2_screen_render(screen);
}

void app_init(app_context_t *ctx) {
    os_log(TAG, "Settings app initializing");

    if (!text_mode_init()) {
        os_log(TAG, "Failed to init text mode");
        return;
    }

    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH | EVENT_TIMER;
    ctx->timer_interval_ms = 100;

    state = STATE_MAIN;
    input_ssid[0] = '\0';
    input_password[0] = '\0';
    scan_selected = 0;

    os_settings_get_string(SETTINGS_KEY_TIMEZONE, "UTC", input_timezone, sizeof(input_timezone));
    os_settings_get_string(SETTINGS_KEY_LOCATION, "40.4168,-3.7038", input_location, sizeof(input_location));

    build_font_family_items();

    char current_font[32];
    os_settings_get_string(SETTINGS_KEY_DEFAULT_FONT, "hack 8", current_font, sizeof(current_font));
    font_id_t current_id = font_lookup_by_name(current_font);
    if (current_id < 0) current_id = FONT_BOOT;

    font_family_selected = 0;
    strncpy(selected_family, font_table[current_id].family, sizeof(selected_family) - 1);
    selected_family[sizeof(selected_family) - 1] = '\0';
    for (int i = 0; i < font_family_count; i++) {
        if (strcmp(font_family_items[i], selected_family) == 0) {
            font_family_selected = i;
            break;
        }
    }

    build_font_size_items(selected_family);
    font_size_selected = 0;
    for (int i = 0; i < font_size_count; i++) {
        int size = 0;
        sscanf(font_size_items[i], "%d", &size);
        if (size == font_table[current_id].size) {
            font_size_selected = i;
            break;
        }
    }

    screen = ui2_screen_create();

    font_family_list = NULL;
    font_size_list = NULL;

    setup_and_render();
    os_log(TAG, "Settings app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    os_log(TAG, "Settings app cleanup");

    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
    }
    tv = NULL;
    toolbar = NULL;
    scan_list = NULL;
    scan_toolbar = NULL;
    font_family_list = NULL;
    font_size_list = NULL;
    for (int s = 0; s < SECTION_COUNT; s++) {
        tab_content[s].list = NULL;
    }

    text_mode_clear(TEXT_COLOR_BLACK);
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;

    if (event->type == EVENT_TIMER) {
        if (state == STATE_WIFI_CONNECTING) {
            if (wifi_is_connected()) {
                ui2_screen_toast_show(screen, "Connected!", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
                state = STATE_MAIN;
                setup_and_render();
            } else if (esp_timer_get_time() - wifi_connect_start > 8000000) {
                if (wifi_used_stored_password) {
                    state = STATE_ENTER_PASSWORD;
                    input_password[0] = '\0';
                    if (!keyboard_is_available())
                        ui2_osk_input_text("WiFi Password", input_password, sizeof(input_password), NULL, true);
                    setup_and_render();
                } else {
                    ui2_screen_toast_show(screen, "Connection failed", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
                    state = STATE_MAIN;
                    setup_and_render();
                }
            }
        }
        if (ble_scan_pending) {
            ble_scan_pending = false;
            ble_keyboard_start_scan(10);

            int ble_count = ble_keyboard_get_scan_count();
            scan_count = ble_count;
            for (int i = 0; i < scan_count && i < 20; i++) {
                const char *name = ble_keyboard_get_scan_name(i);
                int rssi = ble_keyboard_get_scan_rssi(i);
                snprintf(scan_labels[i], sizeof(scan_labels[i]), "%-24s %3ddBm", name, rssi);
                scan_items[i] = scan_labels[i];
            }
            if (scan_list) {
                ui2_list_set_items(scan_list, scan_items, scan_count);
                ui2_list_set_selection(scan_list, 0);
            }

            if (ble_count == 0) {
                ui2_screen_toast_show(screen, "No BLE devices found", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, 6);
            }
            force_render();
        }
        if (ui2_screen_toast_active(screen)) {
            ui2_screen_toast_tick(screen);
            if (!ui2_screen_toast_active(screen)) {
                force_render();
            }
        }
        return;
    }

    if (event->type != EVENT_KEYBOARD && event->type != EVENT_TOUCH) return;

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

                if (ui2_osk_is_active()) {
            ui2_osk_handle_event(NULL, event);
            if (!ui2_osk_is_active()) {
                ui2_osk_result_t result = ui2_osk_get_result();
                if (result == UI2_OSK_RESULT_CONFIRMED) {
                    handle_text_input_confirm();
                } else {
                    state = (state == STATE_ENTER_PASSWORD) ? STATE_SCAN_RESULTS : STATE_MAIN;
                }
                setup_and_render();
            }
            return;
        }

        if (key == 'q' || key == 'Q') {
            os_exit();
            return;
        }

        if (key == '\n' && (state == STATE_ENTER_PASSWORD || state == STATE_ENTER_TIMEZONE || state == STATE_ENTER_LOCATION)) {
            handle_text_input_confirm();
            setup_and_render();
            return;
        }

        if (key == 27 && state != STATE_MAIN) {
            state = (state == STATE_ENTER_PASSWORD) ? STATE_SCAN_RESULTS : STATE_MAIN;
            setup_and_render();
            return;
        }

        if (state == STATE_SCAN_RESULTS && scan_list) {
            if (key == 'w' || key == 'W') {
                UI2_WIDGET(scan_list)->vtable->handle_key(UI2_WIDGET(scan_list), 'w');
                scan_selected = ui2_list_get_selection(scan_list);
                force_render();
                return;
            }
            if (key == 's' || key == 'S') {
                UI2_WIDGET(scan_list)->vtable->handle_key(UI2_WIDGET(scan_list), 's');
                scan_selected = ui2_list_get_selection(scan_list);
                force_render();
                return;
            }
            if (key == '\n' || key == '\r') {
                on_scan_activated(scan_selected, NULL);
                return;
            }
        }
    }

    if (ui2_osk_is_active()) {
        ui2_osk_handle_event(NULL, event);
        if (!ui2_osk_is_active()) {
            ui2_osk_result_t result = ui2_osk_get_result();
            if (result == UI2_OSK_RESULT_CONFIRMED) {
                handle_text_input_confirm();
            } else {
                state = (state == STATE_ENTER_PASSWORD) ? STATE_SCAN_RESULTS : STATE_MAIN;
            }
            setup_and_render();
        }
        return;
    }

    app_state_t prev_state = state;
    bool handled = ui2_screen_handle_event(screen, event);

    if (!ui2_osk_is_active()) {
        if (state != prev_state) {
            setup_and_render();
        } else if (handled) {
            force_render();
        }
    }
}
