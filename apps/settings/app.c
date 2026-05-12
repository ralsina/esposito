#include "os_core.h"
#include "text_mode.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "settings";

#define KEY_ESC 27
#define KEY_BS  8
#define KEY_DEL 127

typedef enum {
    STATE_MAIN,
    STATE_SCAN_RESULTS,
    STATE_ENTER_SSID,
    STATE_ENTER_PASSWORD,
    STATE_MESSAGE,
} app_state_t;

static app_state_t state = STATE_MAIN;
static char input_ssid[WIFI_MAX_SSID] = {0};
static char input_password[WIFI_MAX_PASSWORD] = {0};
static char status_msg[128] = {0};
static int msg_timer = 0;
static int selected = 0;
static int scan_count = 0;
static int scan_selected = 0;
static int prev_selected = -1;

static void set_status(const char *msg) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    msg_timer = 150;
}

#define MENU_ITEMS 5
static const char *menu_labels[MENU_ITEMS] = {
    "Scan for networks",
    "Enter SSID",
    "Enter Password",
    "Save && Connect",
    "Disconnect",
};

static void draw_main(void) {
    text_mode_clear(TEXT_COLOR_BLACK);

    int y = 0;
    text_mode_printf_at_attr((TEXT_MODE_COLS - 13) / 2, y++, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD, "  WiFi Setup  ");

    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, y, "-", TEXT_COLOR_BLUE);
    }
    y++;

    if (wifi_is_connected()) {
        text_mode_printf_at_attr(3, y++, TEXT_COLOR_GREEN, TEXT_ATTR_NORMAL, "Status: Connected");
        text_mode_printf_at_attr(3, y++, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL, "IP: %s", wifi_get_ip());
    } else {
        text_mode_print_at_attr(3, y++, "Status: Disconnected", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    }
    y++;

    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, y, "-", TEXT_COLOR_BLUE);
    }
    y++;

    for (int i = 0; i < MENU_ITEMS; i++) {
        uint16_t color = (i == selected) ? TEXT_COLOR_GREEN : TEXT_COLOR_WHITE;
        char marker = (i == selected) ? '>' : ' ';
        text_mode_printf_at_color(5, y + i, color, "%c  %s", marker, menu_labels[i]);
    }

    y += MENU_ITEMS + 1;
    if (status_msg[0]) {
        text_mode_printf_at_attr(3, y, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD, "%s", status_msg);
    }

    text_mode_print_at_color(3, TEXT_MODE_ROWS - 2, "W/S Navigate  Enter Select  ESC Exit", TEXT_COLOR_WHITE);
}

static void draw_scan_results(void) {
    text_mode_clear(TEXT_COLOR_BLACK);

    int y = 0;
    text_mode_printf_at_attr((TEXT_MODE_COLS - 19) / 2, y++, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD, "Available Networks");

    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, y, "-", TEXT_COLOR_BLUE);
    }
    y++;

    if (scan_count <= 0) {
        text_mode_print_at_attr(3, y, "No networks found", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
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
            text_mode_printf_at_attr(3, y + i, color, TEXT_ATTR_NORMAL,
                "%c %-24s %3d dBm", marker, ssid, rssi);
        }
    }

    text_mode_print_at_color(3, TEXT_MODE_ROWS - 3,
        "W/S Navigate  Enter Select  ESC Back", TEXT_COLOR_WHITE);
}

static void draw_text_entry(const char *title, const char *label, const char *value) {
    text_mode_clear(TEXT_COLOR_BLACK);

    int y = 0;
    text_mode_printf_at_attr((TEXT_MODE_COLS - strlen(title) - 4) / 2, y++, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD, "  %s  ", title);

    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, y, "-", TEXT_COLOR_BLUE);
    }
    y += 2;

    text_mode_printf_at_attr(3, y++, TEXT_COLOR_WHITE, TEXT_ATTR_BOLD, "%s:", label);
    text_mode_printf_at_color(3, y++, TEXT_COLOR_BRIGHT_GREEN, "%s_", value);

    text_mode_print_at_color(3, TEXT_MODE_ROWS - 3,
        "Type to enter  Enter Confirm  ESC Cancel", TEXT_COLOR_WHITE);
}

static void draw_message(void) {
    text_mode_clear(TEXT_COLOR_BLACK);

    int y = 0;
    text_mode_printf_at_attr((TEXT_MODE_COLS - 10) / 2, y++, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD, "  Message  ");

    for (int x = 0; x < TEXT_MODE_COLS; x++) {
        text_mode_print_at_color(x, y, "-", TEXT_COLOR_BLUE);
    }
    y += 2;

    text_mode_printf_at_attr(3, y, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD, "%s", status_msg);
    text_mode_print_at_color(3, TEXT_MODE_ROWS - 3, "Press any key", TEXT_COLOR_WHITE);
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
            draw_text_entry("Enter SSID", "SSID", input_ssid);
            break;
        case STATE_ENTER_PASSWORD:
            draw_text_entry("Enter Password", "Password", input_password);
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
    prev_selected = -1;

    render();
    os_log(TAG, "Settings app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    text_mode_save();
}

void app_close(app_context_t *ctx) {
    os_log(TAG, "Settings app cleanup");
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
                render();
                break;
        }
    } else if (key == KEY_ESC) {
        return;
    }

    if (selected != old) {
        render();
    }
}

static void handle_scan_key(char key) {
    if (key == KEY_ESC) {
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

static void handle_text_entry(char key) {
    char *buf;
    int max_len;

    if (state == STATE_ENTER_SSID) {
        buf = input_ssid;
        max_len = sizeof(input_ssid) - 1;
    } else {
        buf = input_password;
        max_len = sizeof(input_password) - 1;
    }

    if (key == KEY_ESC) {
        state = STATE_MAIN;
        render();
        return;
    }
    if (key == '\n' || key == '\r') {
        state = STATE_MAIN;
        render();
        return;
    }
    if (key == KEY_BS || key == KEY_DEL) {
        size_t len = strlen(buf);
        if (len > 0) buf[len - 1] = '\0';
        render();
        return;
    }
    if (key >= 32 && key <= 126) {
        size_t len = strlen(buf);
        if (len < (size_t)max_len) {
            buf[len] = key;
            buf[len + 1] = '\0';
        }
        render();
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
                handle_text_entry(key);
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
