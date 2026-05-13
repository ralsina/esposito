#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "settings";

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

    ui_separator(y++);
    ui_menu_draw(5, y, MENU_ITEMS, menu_labels, MENU_ITEMS, selected);
    y += MENU_ITEMS + 1;

    if (status_msg[0]) {
        ui_label_attr(3, y, status_msg, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
    }

    ui_status_bar(TEXT_MODE_ROWS - 2, "W/S Navigate  Enter Select", "ESC Exit");
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

static void draw_text_entry(const char *title, const char *label, const char *value) {
    ui_clear();

    int y = 0;
    int tlen = (int)strlen(title);
    ui_label_attr((TEXT_MODE_COLS - tlen - 4) / 2, y++, title, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui_separator(y++);
    y++;

    ui_label_attr(3, y++, label, TEXT_COLOR_WHITE, TEXT_ATTR_BOLD);

    char display[64];
    snprintf(display, sizeof(display), "%s_", value);
    ui_label(3, y, display, TEXT_COLOR_BRIGHT_GREEN);

    ui_status_bar(TEXT_MODE_ROWS - 3, "Type to enter  Enter Confirm", "ESC Cancel");
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
            draw_text_entry("Enter SSID", "SSID:", input_ssid);
            break;
        case STATE_ENTER_PASSWORD:
            draw_text_entry("Enter Password", "Password:", input_password);
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
                break;
            case 2:
                state = STATE_ENTER_PASSWORD;
                input_password[0] = '\0';
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

    int result = ui_text_input_handle(key, buf, max_len);
    if (result != 0) {
        state = STATE_MAIN;
    }
    render();
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
