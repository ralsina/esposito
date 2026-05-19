#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "ui_list.h"
#include "wifi.h"
#include "app_config.h"
#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "settings";

typedef enum {
    STATE_MAIN,
    STATE_SCAN_RESULTS,
    STATE_ENTER_SSID,
    STATE_ENTER_PASSWORD,
    STATE_ENTER_TIMEZONE,
    STATE_ENTER_LOCATION,
    STATE_FONT_SELECTION,
    STATE_FONT_SIZE_SELECTION,
    STATE_MESSAGE,
} app_state_t;

static app_state_t state = STATE_MAIN;
static char input_ssid[WIFI_MAX_SSID] = {0};
static char input_password[WIFI_MAX_PASSWORD] = {0};
static char input_timezone[48] = {0};
static char input_location[64] = {0};
static char status_msg[128] = {0};
static int msg_timer = 0;
static int scan_count = 0;
static int scan_selected = 0;
static ui_text_input_widget_t *ssid_input;
static ui_text_input_widget_t *password_input;
static ui_text_input_widget_t *timezone_input;
static ui_text_input_widget_t *location_input;

// Font family selection
static char font_family_labels[FONT_COUNT][24];
static const char *font_family_items[FONT_COUNT];
static int font_family_count = 0;
static int font_family_selected = 0;
static char selected_family[24];
static ui_list_widget_t *font_family_list;

// Font size selection
static char font_size_labels[FONT_COUNT][48];
static const char *font_size_items[FONT_COUNT];
static int font_size_count = 0;
static int font_size_selected = 0;
static ui_list_widget_t *font_size_list;
static bool layout_needs_rebuild = false;

typedef enum {
    MAIN_FOCUS_LEFT,
    MAIN_FOCUS_RIGHT,
} main_focus_t;

typedef enum {
    SECTION_WIFI,
    SECTION_TIME,
    SECTION_DISPLAY,
    SECTION_DEBUG,
    SECTION_COUNT,
} settings_section_t;

typedef enum {
    ACTION_SCAN,
    ACTION_ENTER_SSID,
    ACTION_ENTER_PASSWORD,
    ACTION_SAVE_CONNECT,
    ACTION_DISCONNECT,
    ACTION_SET_TIMEZONE,
    ACTION_SET_LOCATION,
    ACTION_SET_FONT_FAMILY,
    ACTION_SET_FONT_SIZE,
    ACTION_SET_ROTATION,
    ACTION_TOGGLE_SERIAL,
} settings_action_t;

typedef struct {
    const char *label;
    settings_action_t action;
} section_option_t;

static const char *section_labels[SECTION_COUNT] = {
    "WiFi",
    "Time",
    "Display",
    "Debug",
};

static const section_option_t wifi_options[] = {
    {"Scan", ACTION_SCAN},
    {"SSID", ACTION_ENTER_SSID},
    {"Pass", ACTION_ENTER_PASSWORD},
    {"Save+Conn", ACTION_SAVE_CONNECT},
    {"Disconnect", ACTION_DISCONNECT},
};

static const section_option_t time_options[] = {
    {"Timezone", ACTION_SET_TIMEZONE},
    {"Location", ACTION_SET_LOCATION},
};

static const section_option_t display_options[] = {
    {"Font Family", ACTION_SET_FONT_FAMILY},
    {"Font Size", ACTION_SET_FONT_SIZE},
    {"Rotation", ACTION_SET_ROTATION},
};

static const section_option_t debug_options[] = {
    {"Serial UART", ACTION_TOGGLE_SERIAL},
};

static main_focus_t main_focus = MAIN_FOCUS_LEFT;
static settings_section_t selected_section = SECTION_WIFI;
static int section_option_selected[SECTION_COUNT] = {0};

// Forward declarations for callbacks used by widget rebuild.
static void on_font_family_selection_changed(ui_list_widget_t *list, int new_selection, void *user_data);
static void on_font_family_item_selected(ui_list_widget_t *list, int item_index, void *user_data);
static void on_font_size_selection_changed(ui_list_widget_t *list, int new_selection, void *user_data);
static void on_font_size_item_selected(ui_list_widget_t *list, int item_index, void *user_data);
static void render(void);

#define SETTINGS_KEY_TIMEZONE "time/timezone"
#define SETTINGS_KEY_LOCATION "weather/location"
#define SETTINGS_KEY_SERIAL_LOG "system/serial_log_output"
#define SETTINGS_KEY_DEFAULT_FONT "system/default_font"
#define SETTINGS_KEY_SCREEN_ROTATION "display/rotation"
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

static void rebuild_layout_widgets(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

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
    if (font_family_list) {
        ui_list_destroy(font_family_list);
        font_family_list = NULL;
    }
    if (font_size_list) {
        ui_list_destroy(font_size_list);
        font_size_list = NULL;
    }

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

    // Create font family selection list
    int list_height = rows - 4;
    font_family_list = ui_list_create(2, 2, cols - 4, list_height);
    ui_list_set_title(font_family_list, "Select Font Family");
    ui_list_set_border(font_family_list, true);
    ui_list_set_scrollbar(font_family_list, true);

    ui_list_set_items(font_family_list, font_family_items, font_family_count);
    ui_list_set_selection(font_family_list, font_family_selected);
    ui_list_set_callbacks(font_family_list, on_font_family_selection_changed, on_font_family_item_selected, font_family_list);

    // Create font size selection list
    font_size_list = ui_list_create(2, 2, cols - 4, list_height);
    ui_list_set_title(font_size_list, "Select Font Size");
    ui_list_set_border(font_size_list, true);
    ui_list_set_scrollbar(font_size_list, true);

    ui_list_set_items(font_size_list, font_size_items, font_size_count);
    ui_list_set_selection(font_size_list, font_size_selected);
    ui_list_set_callbacks(font_size_list, on_font_size_selection_changed, on_font_size_item_selected, font_size_list);
}

static void build_font_family_items(void) {
    font_family_count = 0;
    for (int index = 0; index < FONT_COUNT; index++) {
        const char *family = font_table[index].family;
        int found = 0;
        for (int j = 0; j < font_family_count; j++) {
            if (strcmp(font_family_labels[j], family) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            strncpy(font_family_labels[font_family_count], family, sizeof(font_family_labels[0]) - 1);
            font_family_labels[font_family_count][sizeof(font_family_labels[0]) - 1] = '\0';
            font_family_items[font_family_count] = font_family_labels[font_family_count];
            font_family_count++;
        }
    }
}

static void build_font_size_items(const char *family) {
    font_size_count = 0;
    for (int index = 0; index < FONT_COUNT; index++) {
        if (strcmp(font_table[index].family, family) == 0) {
            int cols = display_get_width() / font_table[index].char_width;
            int rows = display_get_height() / font_table[index].char_height;
            snprintf(font_size_labels[font_size_count], sizeof(font_size_labels[0]),
                     "%d (%dx%d)", font_table[index].size, cols, rows);
            font_size_items[font_size_count] = font_size_labels[font_size_count];
            font_size_count++;
        }
    }
}

static font_id_t find_font_by_family_size(const char *family, int size) {
    for (int index = 0; index < FONT_COUNT; index++) {
        if (strcmp(font_table[index].family, family) == 0 && font_table[index].size == size) {
            return font_table[index].id;
        }
    }
    return FONT_HACK_8;
}

// Font family list callbacks
static void on_font_family_selection_changed(ui_list_widget_t *list, int new_selection, void *user_data) {
    (void)list;
    (void)user_data;
    font_family_selected = new_selection;
}

static void on_font_family_item_selected(ui_list_widget_t *list, int item_index, void *user_data) {
    (void)list;
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
        if (font_id >= 0 && font_id < FONT_COUNT) {
            os_settings_set_string(SETTINGS_KEY_DEFAULT_FONT, font_table[font_id].name);
            extern bool text_mode_set_font(font_id_t font);
            text_mode_set_font(font_id);
            layout_needs_rebuild = true;
            set_status("Font changed and saved");
        }
    }

    state = STATE_MAIN;
}

// Font size list callbacks
static void on_font_size_selection_changed(ui_list_widget_t *list, int new_selection, void *user_data) {
    (void)list;
    (void)user_data;
    font_size_selected = new_selection;
}

static void on_font_size_item_selected(ui_list_widget_t *list, int item_index, void *user_data) {
    (void)list;
    (void)user_data;
    if (item_index >= 0 && item_index < font_size_count) {
        // Determine the size value from the label (first token before space)
        int size = 0;
        sscanf(font_size_items[item_index], "%d", &size);

        font_id_t font_id = find_font_by_family_size(selected_family, size);
        if (font_id >= 0 && font_id < FONT_COUNT) {
            os_settings_set_string(SETTINGS_KEY_DEFAULT_FONT, font_table[font_id].name);
            extern font_id_t font_lookup_by_name(const char *name);
            extern bool text_mode_set_font(font_id_t font);
            text_mode_set_font(font_id);
            rebuild_layout_widgets();
            set_status("Font changed and saved");
        }
        state = STATE_MAIN;
    }
}

static void truncate_text(const char *text, char *out, size_t out_size, int max_chars) {
    if (!text || !out || out_size == 0) {
        return;
    }

    if (max_chars <= 0) {
        out[0] = '\0';
        return;
    }

    size_t source_len = strlen(text);
    int usable = max_chars;
    if (usable > (int)out_size - 1) {
        usable = (int)out_size - 1;
    }

    if ((int)source_len <= usable) {
        snprintf(out, out_size, "%s", text);
        return;
    }

    if (usable == 1) {
        out[0] = '~';
        out[1] = '\0';
        return;
    }

    memcpy(out, text, (size_t)(usable - 1));
    out[usable - 1] = '~';
    out[usable] = '\0';
}

static const section_option_t *section_options(settings_section_t section, int *count_out) {
    if (count_out) {
        *count_out = 0;
    }

    switch (section) {
        case SECTION_WIFI:
            if (count_out) *count_out = (int)(sizeof(wifi_options) / sizeof(wifi_options[0]));
            return wifi_options;
        case SECTION_TIME:
            if (count_out) *count_out = (int)(sizeof(time_options) / sizeof(time_options[0]));
            return time_options;
        case SECTION_DISPLAY:
            if (count_out) *count_out = (int)(sizeof(display_options) / sizeof(display_options[0]));
            return display_options;
        case SECTION_DEBUG:
            if (count_out) *count_out = (int)(sizeof(debug_options) / sizeof(debug_options[0]));
            return debug_options;
        default:
            return NULL;
    }
}

static void format_action_value(settings_action_t action, char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    switch (action) {
        case ACTION_SCAN:
            snprintf(out, out_size, "%s", wifi_is_connected() ? "online" : "offline");
            break;
        case ACTION_ENTER_SSID:
            snprintf(out, out_size, "%s", input_ssid[0] ? input_ssid : "(unset)");
            break;
        case ACTION_ENTER_PASSWORD:
            snprintf(out, out_size, "%s", input_password[0] ? "********" : "(empty)");
            break;
        case ACTION_SAVE_CONNECT:
            snprintf(out, out_size, "%s", "apply");
            break;
        case ACTION_DISCONNECT:
            snprintf(out, out_size, "%s", "now");
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
            if (current_id >= 0) {
                snprintf(out, out_size, "%s", font_table[current_id].family);
            } else {
                snprintf(out, out_size, "hack");
            }
            break;
        }
        case ACTION_SET_FONT_SIZE: {
            char current_font[32];
            os_settings_get_string(SETTINGS_KEY_DEFAULT_FONT, "hack 8", current_font, sizeof(current_font));
            font_id_t current_id = font_lookup_by_name(current_font);
            if (current_id >= 0) {
                snprintf(out, out_size, "%d", font_table[current_id].size);
            } else {
                snprintf(out, out_size, "8");
            }
            break;
        }
        case ACTION_SET_ROTATION: {
            int current = os_settings_get_int(SETTINGS_KEY_SCREEN_ROTATION, 1);
            const char *rot_names[] = {"0°", "90°", "180°", "270°"};
            snprintf(out, out_size, "%s", rot_names[current]);
            break;
        }
        case ACTION_TOGGLE_SERIAL:
            snprintf(out, out_size, "%s", serial_log_output_is_enabled() ? "on" : "off");
            break;
        default:
            break;
    }
}

static void execute_main_action(settings_action_t action) {
    switch (action) {
        case ACTION_SCAN:
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
        case ACTION_ENTER_SSID:
            state = STATE_ENTER_SSID;
            input_ssid[0] = '\0';
            render();
            break;
        case ACTION_ENTER_PASSWORD:
            state = STATE_ENTER_PASSWORD;
            input_password[0] = '\0';
            render();
            break;
        case ACTION_SAVE_CONNECT:
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
        case ACTION_DISCONNECT:
            wifi_disconnect();
            set_status("Disconnected");
            render();
            break;
        case ACTION_SET_TIMEZONE:
            state = STATE_ENTER_TIMEZONE;
            render();
            break;
        case ACTION_SET_LOCATION:
            state = STATE_ENTER_LOCATION;
            render();
            break;
        case ACTION_SET_FONT_FAMILY:
            state = STATE_FONT_SELECTION;
            render();
            break;
        case ACTION_SET_FONT_SIZE:
            build_font_size_items(selected_family);
            font_size_selected = 0;
            {
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
            }
            if (font_size_list) {
                ui_list_set_items(font_size_list, font_size_items, font_size_count);
                ui_list_set_selection(font_size_list, font_size_selected);
            }
            state = STATE_FONT_SIZE_SELECTION;
            render();
            break;
        case ACTION_SET_ROTATION: {
            int current = os_settings_get_int(SETTINGS_KEY_SCREEN_ROTATION, 1);
            int new_rotation = (current + 1) % 4;
            os_settings_set_int(SETTINGS_KEY_SCREEN_ROTATION, new_rotation);
            display_set_rotation(new_rotation);
            const char *rot_names[] = {"0° (Portrait)", "90° (Landscape)", "180° (Inverted Portrait)", "270° (Inverted Landscape)"};
            snprintf(status_msg, sizeof(status_msg), "Rotation: %s", rot_names[new_rotation]);
            set_status(status_msg);
            render();
            break;
        }
        case ACTION_TOGGLE_SERIAL: {
            bool enabled = !serial_log_output_is_enabled();
            serial_log_output_set_enabled(enabled);
            os_settings_set_bool(SETTINGS_KEY_SERIAL_LOG, enabled);
            set_status(enabled ? "Serial log output enabled" : "Serial log output disabled");
            render();
            break;
        }
    }
}

static void draw_main_split_layout(void) {
    const int cols = text_mode_get_cols();
    const int rows = text_mode_get_rows();
    const int left_width = 11;
    const int divider_col = left_width;
    const int right_x = left_width + 1;
    const int right_width = cols - right_x;
    const int content_top = 2;
    const int content_bottom = rows - 3;
    const int content_height = content_bottom - content_top + 1;

    char title[32];
    snprintf(title, sizeof(title), " Settings ");
    ui_label_attr((cols - (int)strlen(title)) / 2, 0, title, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui_separator(1);

    for (int row = content_top; row <= content_bottom; row++) {
        text_mode_print_at_attr(divider_col, row, "|", TEXT_COLOR_BLUE, TEXT_ATTR_LINE_DRAWING);
    }

    for (int row = 0; row < content_height; row++) {
        int section_index = row;
        int screen_y = content_top + row;
        if (section_index >= SECTION_COUNT) {
            break;
        }

        uint8_t color = TEXT_COLOR_WHITE;
        if (section_index == selected_section) {
            color = main_focus == MAIN_FOCUS_LEFT ? TEXT_COLOR_BRIGHT_GREEN : TEXT_COLOR_BRIGHT_CYAN;
        }

        char marker = section_index == selected_section ? '>' : ' ';
        char label_buf[10];
        truncate_text(section_labels[section_index], label_buf, sizeof(label_buf), left_width - 2);
        text_mode_printf_at_attr_bg(0, screen_y, color, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL, "%c%-7s", marker, label_buf);
    }

    int option_count = 0;
    const section_option_t *options = section_options(selected_section, &option_count);
    if (option_count <= 0 || !options) {
        return;
    }

    int selected_option = section_option_selected[selected_section];
    if (selected_option < 0) selected_option = 0;
    if (selected_option >= option_count) selected_option = option_count - 1;
    section_option_selected[selected_section] = selected_option;

    for (int row = 0; row < content_height; row++) {
        int option_index = row;
        int screen_y = content_top + row;
        if (option_index >= option_count) {
            break;
        }

        char value[80];
        char line[96];
        char clipped[96];
        format_action_value(options[option_index].action, value, sizeof(value));
        if (value[0]) {
            snprintf(line, sizeof(line), "%s: %s", options[option_index].label, value);
        } else {
            snprintf(line, sizeof(line), "%s", options[option_index].label);
        }

        truncate_text(line, clipped, sizeof(clipped), right_width - 2);

        uint8_t color = TEXT_COLOR_WHITE;
        if (option_index == selected_option) {
            color = main_focus == MAIN_FOCUS_RIGHT ? TEXT_COLOR_BRIGHT_GREEN : TEXT_COLOR_BRIGHT_CYAN;
        }
        char marker = option_index == selected_option ? '>' : ' ';
        text_mode_printf_at_attr_bg(right_x, screen_y, color, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL, "%c%s", marker, clipped);
    }

    ui_status_bar(rows - 2, "W/S move  A/D pane  Enter select  Esc back", status_msg[0] ? status_msg : "Settings");
}

static void draw_main(void) {
    ui_clear();
    draw_main_split_layout();
}

static void draw_scan_results(void) {
    ui_clear();

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int y = 0;
    ui_label_attr((cols - 19) / 2, y++, "Available Networks", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui_separator(y++);

    if (scan_count <= 0) {
        ui_label_attr(3, y, "No networks found", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    } else {
        y++;
        if (scan_selected >= scan_count) scan_selected = 0;

        for (int i = 0; i < scan_count && i < rows - 6; i++) {
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

    ui_status_bar(rows - 3, "W/S Navigate  Enter Select", "ESC Back");
}

static void draw_message(void) {
    ui_clear();

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int y = 0;
    ui_label_attr((cols - 10) / 2, y++, "  Message  ", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui_separator(y++);
    y++;

    ui_label_attr(3, y, status_msg, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
    ui_status_bar(rows - 3, "Press any key", "");
}

static void render(void) {
    if (layout_needs_rebuild) {
        rebuild_layout_widgets();
        layout_needs_rebuild = false;
    }
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
        case STATE_FONT_SELECTION:
            ui_clear();
            ui_list_draw(font_family_list);
            break;
        case STATE_FONT_SIZE_SELECTION:
            ui_clear();
            ui_list_draw(font_size_list);
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
    scan_selected = 0;
    main_focus = MAIN_FOCUS_LEFT;
    selected_section = SECTION_WIFI;
    for (int index = 0; index < SECTION_COUNT; index++) {
        section_option_selected[index] = 0;
    }

    serial_log_output_set_enabled(os_settings_get_bool(SETTINGS_KEY_SERIAL_LOG, false));

    os_settings_get_string(SETTINGS_KEY_TIMEZONE, "UTC", input_timezone, sizeof(input_timezone));
    os_settings_get_string(SETTINGS_KEY_LOCATION, "40.4168,-3.7038", input_location, sizeof(input_location));

    build_font_family_items();

    char current_font[32];
    os_settings_get_string(SETTINGS_KEY_DEFAULT_FONT, "hack 8", current_font, sizeof(current_font));
    font_id_t current_id = font_lookup_by_name(current_font);
    if (current_id < 0) current_id = FONT_HACK_8;

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

    rebuild_layout_widgets();

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
    if (font_family_list) {
        ui_list_destroy(font_family_list);
        font_family_list = NULL;
    }
    if (font_size_list) {
        ui_list_destroy(font_size_list);
        font_size_list = NULL;
    }

    text_mode_clear(TEXT_COLOR_BLACK);
}

static void handle_main_key(char key) {
    int option_count = 0;
    const section_option_t *options = section_options(selected_section, &option_count);

    if (key == 'a' || key == 'A') {
        main_focus = MAIN_FOCUS_LEFT;
        render();
        return;
    }

    if (key == 'd' || key == 'D') {
        main_focus = MAIN_FOCUS_RIGHT;
        render();
        return;
    }

    if (key == 'w' || key == 'W') {
        if (main_focus == MAIN_FOCUS_LEFT) {
            selected_section = (selected_section - 1 + SECTION_COUNT) % SECTION_COUNT;
        } else if (option_count > 0) {
            int selection = section_option_selected[selected_section];
            selection = (selection - 1 + option_count) % option_count;
            section_option_selected[selected_section] = selection;
        }
        render();
        return;
    }

    if (key == 's' || key == 'S') {
        if (main_focus == MAIN_FOCUS_LEFT) {
            selected_section = (selected_section + 1) % SECTION_COUNT;
        } else if (option_count > 0) {
            int selection = section_option_selected[selected_section];
            selection = (selection + 1) % option_count;
            section_option_selected[selected_section] = selection;
        }
        render();
        return;
    }

    if (key == '\n' || key == '\r') {
        if (main_focus == MAIN_FOCUS_LEFT) {
            main_focus = MAIN_FOCUS_RIGHT;
            render();
            return;
        }

        if (options && option_count > 0) {
            int selection = section_option_selected[selected_section];
            if (selection < 0) selection = 0;
            if (selection >= option_count) selection = option_count - 1;
            section_option_selected[selected_section] = selection;
            execute_main_action(options[selection].action);
            return;
        }
    }

    if (key == 27) {
        if (main_focus == MAIN_FOCUS_RIGHT) {
            main_focus = MAIN_FOCUS_LEFT;
            render();
        }
        return;
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
            case STATE_FONT_SELECTION:
                if (key == 27) {
                    state = STATE_MAIN;
                } else {
                    ui_list_handle_key(font_family_list, key);
                }
                render();
                break;
            case STATE_FONT_SIZE_SELECTION:
                if (key == 27) {
                    state = STATE_MAIN;
                } else {
                    ui_list_handle_key(font_size_list, key);
                }
                render();
                break;
            case STATE_MESSAGE:
                handle_message_key(key);
                break;
        }
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        // Convert pixel coordinates to character coordinates
        int cw = text_mode_get_char_width();
        int ch = text_mode_get_char_height();
        int x_col = event->touch.x / cw;
        int y_col = event->touch.y / ch;

        // Create a modified touch event with character coordinates
        event_t char_event = *event;
        char_event.touch.x = x_col;
        char_event.touch.y = y_col;

        switch (state) {
            case STATE_FONT_SELECTION:
                ui_list_handle_touch(font_family_list, &char_event);
                render();
                break;
            case STATE_FONT_SIZE_SELECTION:
                ui_list_handle_touch(font_size_list, &char_event);
                render();
                break;
            default:
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
