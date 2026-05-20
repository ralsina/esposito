#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "ui_list.h"
#include "ui_text_input.h"
#include "core_json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "lali";

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

static char lines[MAX_LINES][MAX_LINE_LEN];
static const char *items[MAX_LINES];
static int line_count = 0;
static char input_buffer[INPUT_BUF_LEN];
static char api_key[API_KEY_MAX] = {0};
static char response_buf[RESPONSE_BUF];

static ui_list_widget_t *list = NULL;
static ui_text_input_widget_t *text_input = NULL;

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
            case '"':
                if (j < output_size - 2) { output[j++] = '\\'; output[j++] = '"'; }
                break;
            case '\\':
                if (j < output_size - 2) { output[j++] = '\\'; output[j++] = '\\'; }
                break;
            case '\n':
                if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'n'; }
                break;
            case '\r':
                if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'r'; }
                break;
            case '\t':
                if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 't'; }
                break;
            default:
                if ((unsigned char)c >= 32) {
                    output[j++] = c;
                }
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
        if (response_buf[0]) {
            return response_buf;
        }
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

static void add_line(const char *line) {
    if (line_count < MAX_LINES) {
        strncpy(lines[line_count], line, MAX_LINE_LEN - 1);
        lines[line_count][MAX_LINE_LEN - 1] = '\0';
        items[line_count] = lines[line_count];
        line_count++;
    } else {
        memmove(lines[0], lines[1], (MAX_LINES - 1) * MAX_LINE_LEN);
        memmove((void *)items[0], (const void *)items[1], (MAX_LINES - 1) * sizeof(char *));
        strncpy(lines[MAX_LINES - 1], line, MAX_LINE_LEN - 1);
        lines[MAX_LINES - 1][MAX_LINE_LEN - 1] = '\0';
        items[MAX_LINES - 1] = lines[MAX_LINES - 1];
    }
}

static void update_list(void) {
    ui_list_set_items(list, items, line_count);
    ui_list_scroll_to_item(list, line_count - 1);
    ui_list_set_selection(list, line_count - 1);
}

static void render(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    ui_list_draw(list);
    ui_text_input_draw(text_input);
    text_mode_flush();
}

static void add_response(const char *speaker, const char *text) {
    int max_cols = text_mode_get_cols();
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
            if (*text == ' ' && col > 0) {
                last_space = text;
            }
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
        add_line(line);

        while (*text == ' ') text++;
    }
}

static void show_thinking(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    ui_list_draw(list);
    text_mode_print_at_attr(0, text_mode_get_rows() - 1,
                            "  waiting for response...", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    text_mode_flush();
}

static void on_confirm(ui_text_input_widget_t *widget, void *user_data) {
    (void)user_data;
    char saved_input[INPUT_BUF_LEN];
    strncpy(saved_input, widget->buffer, INPUT_BUF_LEN - 1);
    saved_input[INPUT_BUF_LEN - 1] = '\0';

    if (saved_input[0] == '\0') {
        return;
    }

    char line[MAX_LINE_LEN];
    snprintf(line, sizeof(line), "  You: %s", saved_input);
    add_line(line);
    update_list();
    ui_text_input_clear(text_input);
    render();

    show_thinking();

    const char *answer = call_openrouter(saved_input);
    add_response("Lali", answer);
    update_list();
    render();
}

void app_init(app_context_t *ctx) {
    if (!text_mode_init()) {
        os_log(TAG, "text_mode_init failed");
        return;
    }

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    input_buffer[0] = '\0';

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    list = ui_list_create(0, 0, cols, rows - 3);
    ui_list_set_title(list, "Lali");
    ui_list_set_items(list, items, 0);

    text_input = ui_text_input_create(0, rows - 3, cols, 3);
    ui_text_input_set_buffer(text_input, input_buffer, INPUT_BUF_LEN);
    ui_text_input_set_label(text_input, ">");
    ui_text_input_set_hints(text_input, "Enter to send", "FN+W/S scroll history");
    ui_text_input_set_callbacks(text_input, NULL, on_confirm, NULL, NULL);
    ui_text_input_set_focus(text_input, true);

    if (!load_api_key()) {
        char line[MAX_LINE_LEN];
        snprintf(line, sizeof(line), "  ! No API key at %s", API_KEY_PATH);
        add_line(line);
        update_list();
    }

    render();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        uint8_t modifiers = event->keyboard.modifiers;

        if ((modifiers & MODIFIER_FN) && (key == 'w' || key == 'W')) {
            ui_list_handle_key(list, 'w');
        } else if ((modifiers & MODIFIER_FN) && (key == 's' || key == 'S')) {
            ui_list_handle_key(list, 's');
        } else {
            ui_text_input_handle_key(text_input, key);
        }
        render();
    }
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    ui_list_destroy(list);
    ui_text_input_destroy(text_input);
    text_mode_clear(TEXT_COLOR_BLACK);
    list = NULL;
    text_input = NULL;
}
