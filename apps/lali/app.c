#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"
#include "ui2.h"
#include "hardware.h"
#include "core_json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "lali";
#define CHECKPOINT_KEY "history"

#define MAX_ENTRIES 100
#define LINES_PER_ENTRY 2
#define MAX_LINES (MAX_ENTRIES * LINES_PER_ENTRY)
#define MAX_LINE_LEN 64
#define INPUT_BUF_LEN 64
#define API_KEY_PATH "/sdcard/openrouter"
#define API_KEY_MAX 128
#define RESPONSE_BUF 4096

static const char *OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions";
static const char *OPENROUTER_MODEL = "z-ai/glm-4.5-air:free";

static const char *SYSTEM_PROMPT =
    "Your name is Lali. Keep responses brief.";

static const char *GTS_ROOT_R4_PEM =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX\n"
    "MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
    "CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx\n"
    "NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
    "GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0\n"
    "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube\n"
    "Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e\n"
    "WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd\n"
    "BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd\n"
    "BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN\n"
    "l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw\n"
    "Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v\n"
    "Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG\n"
    "SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ\n"
    "odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY\n"
    "+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs\n"
    "kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep\n"
    "8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1\n"
    "vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl\n"
    "-----END CERTIFICATE-----\n";

static ui2_buffer_t *buffer = NULL;
static ui2_text_input_t *text_input = NULL;
static char input_buffer[INPUT_BUF_LEN];
static char api_key[API_KEY_MAX] = {0};
static char response_buf[RESPONSE_BUF];
static int cols = 0;
static int rows = 0;

static bool load_api_key(void) {
    FILE *f = fopen(API_KEY_PATH, "r");
    if (!f) {
        return false;
    }
    if (!fgets(api_key, API_KEY_MAX, f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    size_t len = strlen(api_key);
    while (len > 0 && (api_key[len - 1] == '\n' || api_key[len - 1] == '\r')) {
        api_key[--len] = '\0';
    }
    return len > 0;
}

static void json_escape(const char *input, char *output, size_t output_size) {
    size_t j = 0;
    for (size_t i = 0; input[i] && j < output_size - 1; i++) {
        char c = input[i];
        switch (c) {
            case '"': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = '"'; } break;
            case '\\': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = '\\'; } break;
            case '\n': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'n'; } break;
            case '\r': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'r'; } break;
            case '\t': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 't'; } break;
            default:
                if ((unsigned char)c >= 32) output[j++] = c;
                break;
        }
    }
    output[j] = '\0';
}

static const char *call_openrouter(const char *prompt) {
    char escaped_prompt[256];
    json_escape(prompt, escaped_prompt, sizeof(escaped_prompt));

    char body[768];
    snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\",\"content\":\"%s\"}]}",
        OPENROUTER_MODEL, SYSTEM_PROMPT, escaped_prompt);

    char auth_value[256];
    snprintf(auth_value, sizeof(auth_value), "Bearer %s", api_key);
    const char *headers[] = {"Authorization", auth_value, NULL};

    response_buf[0] = '\0';
    os_log(TAG, "POST body: %s", body);
    int result = os_http_post(OPENROUTER_URL, body, headers, GTS_ROOT_R4_PEM,
                              response_buf, sizeof(response_buf), 30000);

    os_log(TAG, "POST result: %d, response: %s", result, response_buf);

    if (result <= 0) {
        if (response_buf[0]) return response_buf;
        snprintf(response_buf, sizeof(response_buf), "HTTP error: %d", result);
        return response_buf;
    }

    if (JSON_Validate(response_buf, strlen(response_buf)) != JSONSuccess) {
        snprintf(response_buf, sizeof(response_buf), "Invalid JSON response");
        return response_buf;
    }

    const char *content = NULL;
    size_t content_len = 0;
    JSONStatus_t js = JSON_SearchConst(response_buf, strlen(response_buf),
        "choices[0].message.content", strlen("choices[0].message.content"),
        &content, &content_len, NULL);

    if (js != JSONSuccess || !content || content_len == 0) {
        snprintf(response_buf, sizeof(response_buf), "Parse error");
        return response_buf;
    }

    size_t copy_len = content_len < sizeof(response_buf) - 1 ? content_len : sizeof(response_buf) - 1;
    memmove(response_buf, content, copy_len);
    response_buf[copy_len] = '\0';
    return response_buf;
}

static void add_line(const char *text, uint8_t fg, uint8_t bg, uint8_t attrs) {
    char line[MAX_LINE_LEN];
    strncpy(line, text, MAX_LINE_LEN - 1);
    line[MAX_LINE_LEN - 1] = '\0';
    ui2_buffer_add_line(buffer, line, fg, bg, attrs);
}

static void render(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    UI2_WIDGET(buffer)->vtable->draw(UI2_WIDGET(buffer));
    UI2_WIDGET(text_input)->vtable->draw(UI2_WIDGET(text_input));
    text_mode_flush();
}

static void add_response(const char *speaker, const char *text, uint8_t fg, uint8_t bg) {
    int max_cols = cols;
    int first_prefix = snprintf(NULL, 0, "  %s: ", speaker);
    int cont_prefix = 2;
    int first_width = max_cols - first_prefix - 1;
    int cont_width = max_cols - cont_prefix - 1;
    if (first_width < 5) first_width = 5;
    if (cont_width < 5) cont_width = 5;

    char line[MAX_LINE_LEN];
    int is_first = 1;

    while (*text) {
        int width = is_first ? first_width : cont_width;
        const char *start = text;
        const char *last_space = NULL;
        int col = 0;

        while (*text && col < width && *text != '\n' && *text != '\r') {
            if (*text == ' ' && col > 0) last_space = text;
            text++;
            col++;
        }

        int line_len;
        if (*text == '\n' || *text == '\r') {
            line_len = text - start;
            text++;
            if (*text == '\n') text++;
        } else if ((col >= width || *text == '\0') && last_space && last_space > start) {
            line_len = last_space - start;
            text = last_space + 1;
        } else if (col >= width) {
            line_len = width;
            text = start + width;
        } else {
            line_len = text - start;
        }

        if (is_first) {
            snprintf(line, sizeof(line), "  %s: %.*s", speaker, line_len, start);
            is_first = 0;
        } else {
            snprintf(line, sizeof(line), "  %.*s", line_len, start);
        }
        add_line(line, fg, bg, TEXT_ATTR_NORMAL);

        while (*text == ' ') text++;
    }
}

static void show_thinking(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    UI2_WIDGET(buffer)->vtable->draw(UI2_WIDGET(buffer));
    text_mode_print_at_attr(0, rows - 1,
                            "  waiting for response...", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    text_mode_flush();
}

static void save_checkpoint(void) {
    printf("save_checkpoint: starting, count=%d\n", ui2_buffer_get_count(buffer));
    size_t total = 0;
    for (int i = 0; i < ui2_buffer_get_count(buffer); i++) {
        ui2_buffer_line_t *line = &buffer->lines[(buffer->head + i) % buffer->max_lines];
        total += strlen(line->text) + 1;
    }
    os_log(TAG, "save_checkpoint: %d lines, %u bytes", ui2_buffer_get_count(buffer), (unsigned)total);
    char *buf = malloc(total + 1);
    if (!buf) return;
    char *p = buf;
    for (int i = 0; i < ui2_buffer_get_count(buffer); i++) {
        ui2_buffer_line_t *line = &buffer->lines[(buffer->head + i) % buffer->max_lines];
        size_t len = strlen(line->text);
        memcpy(p, line->text, len);
        p += len;
        *p++ = '\n';
    }
    *p = '\0';
    os_log(TAG, "save_checkpoint: saving key=%s", CHECKPOINT_KEY);
    config_set_string(CHECKPOINT_KEY, buf);
    free(buf);
}

static void load_checkpoint(void) {
    os_log(TAG, "load_checkpoint: loading key=%s", CHECKPOINT_KEY);
    size_t data_size = 0;
    char *data = config_read_all_alloc(CHECKPOINT_KEY, &data_size);
    if (!data) return;
    os_log(TAG, "load_checkpoint: data=%u bytes", (unsigned)data_size);

    char *p = data;
    while (*p && ui2_buffer_get_count(buffer) < MAX_LINES) {
        char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
        p[len] = '\0';
        ui2_buffer_add_line(buffer, p, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
        p = nl ? nl + 1 : p + len;
    }
    config_free(data);
    os_log(TAG, "load_checkpoint: loaded %d lines", ui2_buffer_get_count(buffer));
}

static void on_confirm(void *user_data) {
    (void)user_data;
    char saved_input[INPUT_BUF_LEN];
    strncpy(saved_input, input_buffer, INPUT_BUF_LEN - 1);
    saved_input[INPUT_BUF_LEN - 1] = '\0';

    if (saved_input[0] == '\0') return;

    char line[MAX_LINE_LEN];
    snprintf(line, sizeof(line), "  You: %s", saved_input);
    add_line(line, TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);
    ui2_text_input_clear(text_input);
    render();

    show_thinking();

    const char *answer = call_openrouter(saved_input);
    add_response("Lali", answer, TEXT_COLOR_BRIGHT_GREEN, TEXT_COLOR_BLACK);
    render();
}

void app_init(app_context_t *ctx) {
    if (!text_mode_init()) {
        os_log(TAG, "text_mode_init failed");
        return;
    }

    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    ctx->timer_interval_ms = 0;

    input_buffer[0] = '\0';

    cols = text_mode_get_cols();
    rows = text_mode_get_rows();

    buffer = ui2_buffer_create(0, 0, cols, rows - 3, MAX_LINES);
    ui2_buffer_set_scroll_to_bottom(buffer, true);

    text_input = ui2_text_input_create(0, rows - 3, cols, 3);
    ui2_text_input_set_buffer(text_input, input_buffer, INPUT_BUF_LEN);
    ui2_text_input_set_title(text_input, "Lali");
    ui2_text_input_set_label(text_input, ">");

    bool has_keyboard = keyboard_is_available();
    if (has_keyboard) {
        ui2_text_input_set_hints(text_input, "Enter to send", "W/S scroll history");
    } else {
        ui2_text_input_set_hints(text_input, "Touch input to type", "Tap buffer to scroll");
    }

    ui2_text_input_set_callbacks(text_input, on_confirm, NULL, NULL);

    os_log(TAG, "app_init: loading checkpoint");
    load_checkpoint();
    os_log(TAG, "app_init: loaded %d lines", ui2_buffer_get_count(buffer));
    ui2_buffer_set_scroll_to_bottom(buffer, true);

    if (!load_api_key()) {
        char line[MAX_LINE_LEN];
        snprintf(line, sizeof(line), "  ! No API key at %s", API_KEY_PATH);
        add_line(line, TEXT_COLOR_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
    }

    render();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (ui2_osk_is_active()) {
        ui2_osk_handle_event(NULL, (event_t*)event);
        if (!ui2_osk_is_active()) {
            ui2_osk_result_t result = ui2_osk_get_result();
            if (result == UI2_OSK_RESULT_CONFIRMED) {
                on_confirm(NULL);
            } else {
                render();
            }
        }
        return;
    }

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        uint8_t modifiers = event->keyboard.modifiers;

        if ((modifiers & MODIFIER_FN) && (key == 'w' || key == 'W')) {
            UI2_WIDGET(buffer)->vtable->handle_key(UI2_WIDGET(buffer), 'w');
        } else if ((modifiers & MODIFIER_FN) && (key == 's' || key == 'S')) {
            UI2_WIDGET(buffer)->vtable->handle_key(UI2_WIDGET(buffer), 's');
        } else {
            UI2_WIDGET(text_input)->vtable->handle_key(UI2_WIDGET(text_input), key);
        }
        render();
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        int cw = text_mode_get_char_width();
        int ch = text_mode_get_char_height();
        int x_col = event->touch.x / cw;
        int y_col = event->touch.y / ch;

        if (y_col >= rows - 3) {
            if (!keyboard_is_available()) {
                ui2_osk_input_text("Message:", input_buffer, INPUT_BUF_LEN, input_buffer, false);
            }
        } else if (y_col < rows - 3) {
            UI2_WIDGET(buffer)->vtable->handle_touch(UI2_WIDGET(buffer), x_col, y_col, true);
            render();
        }
    }
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
    printf("lali checkpoint called, count=%d\n", ui2_buffer_get_count(buffer));
    save_checkpoint();
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    UI2_WIDGET(buffer)->vtable->destroy(UI2_WIDGET(buffer));
    UI2_WIDGET(text_input)->vtable->destroy(UI2_WIDGET(text_input));
    text_mode_clear(TEXT_COLOR_BLACK);
    buffer = NULL;
    text_input = NULL;
}
