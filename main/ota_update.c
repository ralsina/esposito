#include "ota_update.h"
#include "text_mode.h"
#include "app_heap.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

extern bool wifi_is_connected(void);

static const char *TAG = "ota";

static const char OTA_WE1_CA_PEM[] =
"-----BEGIN CERTIFICATE-----\n"
"MIICjjCCAjOgAwIBAgIQf/NXaJvCTjAtkOGKQb0OHzAKBggqhkjOPQQDAjBQMSQw\n"
"IgYDVQQLExtHbG9iYWxTaWduIEVDQyBSb290IENBIC0gUjQxEzARBgNVBAoTCkds\n"
"b2JhbFNpZ24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMjMxMjEzMDkwMDAwWhcN\n"
"MjkwMjIwMTQwMDAwWjA7MQswCQYDVQQGEwJVUzEeMBwGA1UEChMVR29vZ2xlIFRy\n"
"dXN0IFNlcnZpY2VzMQwwCgYDVQQDEwNXRTEwWTATBgcqhkjOPQIBBggqhkjOPQMB\n"
"BwNCAARvzTr+Z1dHTCEDhUDCR127WEcPQMFcF4XGGTfn1XzthkubgdnXGhOlCgP4\n"
"mMTG6J7/EFmPLCaY9eYmJbsPAvpWo4IBAjCB/zAOBgNVHQ8BAf8EBAMCAYYwHQYD\n"
"VR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBIGA1UdEwEB/wQIMAYBAf8CAQAw\n"
"HQYDVR0OBBYEFJB3kjVnxP+ozKnme9mAeXvMk/k4MB8GA1UdIwQYMBaAFFSwe61F\n"
"uOJAf/sKbvu+M8k8o4TVMDYGCCsGAQUFBwEBBCowKDAmBggrBgEFBQcwAoYaaHR0\n"
"cDovL2kucGtpLmdvb2cvZ3NyNC5jcnQwLQYDVR0fBCYwJDAioCCgHoYcaHR0cDov\n"
"L2MucGtpLmdvb2cvci9nc3I0LmNybDATBgNVHSAEDDAKMAgGBmeBDAECATAKBggq\n"
"hkjOPQQDAgNJADBGAiEAokJL0LgR6SOLR02WWxccAq3ndXp4EMRveXMUVUxMWSMC\n"
"IQDspFWa3fj7nLgouSdkcPy1SdOR2AGm9OQWs7veyXsBwA==\n"
"-----END CERTIFICATE-----\n";

const char *ota_firmware_version(void) {
    return FIRMWARE_VERSION;
}

typedef struct {
    char *buf;
    size_t buf_size;
    size_t len;
} ota_http_ctx_t;

static esp_err_t ota_http_event_handler(esp_http_client_event_t *event) {
    ota_http_ctx_t *ctx = (ota_http_ctx_t *)event->user_data;
    if (!ctx || !ctx->buf) return ESP_OK;

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data && event->data_len > 0) {
        size_t writable = ctx->buf_size - 1 - ctx->len;
        size_t to_copy = (size_t)event->data_len;
        if (to_copy > writable) to_copy = writable;
        if (to_copy > 0) {
            memcpy(ctx->buf + ctx->len, event->data, to_copy);
            ctx->len += to_copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static int ota_http_get(const char *url, char *out, size_t out_size, int timeout_ms) {
    if (!url || !out || out_size < 2) return -1;
    out[0] = '\0';

    ota_http_ctx_t ctx = { .buf = out, .buf_size = out_size, .len = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 10000,
        .event_handler = ota_http_event_handler,
        .user_data = &ctx,
        .cert_pem = OTA_WE1_CA_PEM,
        .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .user_agent = "esposito-ota/1.0",
        .addr_type = HTTP_ADDR_TYPE_INET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return -1;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        int socket_errno = esp_http_client_get_errno(client);
        ESP_LOGE(TAG, "HTTP perform FAILED: %s (0x%x), sock_errno=%d (%s)",
                 esp_err_to_name(err), err, socket_errno, strerror(socket_errno));
        esp_http_client_cleanup(client);
        return -1;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) return -status;
    return (int)ctx.len;
}

static void strip_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

bool ota_check_for_update(char *latest_version, size_t max_len) {
    if (!latest_version || max_len < 1) return false;
    latest_version[0] = '\0';

    char buf[128];
    int total = ota_http_get(OTA_VERSION_URL, buf, sizeof(buf), 10000);

    if (total <= 0) return false;

    strip_newline(buf);

    if (strlen(buf) >= max_len) return false;

    strcpy(latest_version, buf);
    printf("[OTA] Current: %s, Latest: %s\n", FIRMWARE_VERSION, latest_version);

    return strcmp(latest_version, FIRMWARE_VERSION) != 0;
}

static void draw_progress(int pct) {
    int cols = text_mode_get_cols();
    if (cols < 8) cols = 8;

    int bar_width = cols - 4;
    int bar_y = 4;
    int filled = (pct * bar_width) / 100;

    for (int i = 0; i < bar_width; i++) {
        uint8_t color = (i < filled) ? TEXT_COLOR_GREEN : TEXT_COLOR_BRIGHT_BLACK;
        uint8_t attr = TEXT_ATTR_BORDER_LEFT | TEXT_ATTR_BORDER_RIGHT | TEXT_ATTR_UNDERLINE | TEXT_ATTR_BORDER_TOP;
        text_mode_print_at_attr_bg(i + 2, bar_y, " ", color, TEXT_COLOR_CYAN, attr);
    }

    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%3d%%", pct);
    text_mode_print_at(2 + (bar_width - 3) / 2, bar_y, pct_str);
    text_mode_flush();
}

static void handoff_to_stub(void) {
    const esp_partition_t *update_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "Update partition (stub) not found");
        return;
    }

    esp_err_t err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot to stub: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Handing off to update stub, rebooting...");
    esp_restart();
}

void ota_recovery_check(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return;

    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        ESP_LOGW(TAG, "Running from update partition - falling back to factory");
        const esp_partition_t *factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
        if (factory) {
            esp_ota_set_boot_partition(factory);
        }
        return;
    }

    FILE *fp = fopen("/sdcard/system/firmware.bin", "rb");
    if (!fp) return;
    fclose(fp);

    ESP_LOGI(TAG, "Found /sdcard/system/firmware.bin, launching update stub...");

    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at(0, 0, "Firmware Update");
    text_mode_print_at(0, 2, "Rebooting to update...");
    text_mode_flush();

    handoff_to_stub();
}

typedef struct {
    FILE *fp;
    int total;
    int content_length;
    int last_pct;
    bool error;
} ota_dl_ctx_t;

static ota_dl_ctx_t s_dl_ctx;

static esp_err_t ota_dl_event_handler(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (event->header_key && strcasecmp(event->header_key, "Content-Length") == 0 && event->header_value) {
            s_dl_ctx.content_length = atoi(event->header_value);
            printf("[OTA] Content-Length: %d\n", s_dl_ctx.content_length);
        }
        return ESP_OK;
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data && event->data_len > 0) {
        if (fwrite(event->data, 1, event->data_len, s_dl_ctx.fp) != event->data_len) {
            s_dl_ctx.error = true;
            return ESP_FAIL;
        }
        s_dl_ctx.total += (int)event->data_len;
        if (s_dl_ctx.content_length > 0) {
            int pct = (s_dl_ctx.total * 100) / s_dl_ctx.content_length;
            if (pct > 100) pct = 100;
            if (pct != s_dl_ctx.last_pct) {
                s_dl_ctx.last_pct = pct;
                draw_progress(pct);
            }
        }
    }
    return ESP_OK;
}

void ota_apply_update(void) {
    printf("\n[OTA] Starting OTA update\n");
    printf("[OTA] URL: %s\n", OTA_FIRMWARE_URL);

    if (!wifi_is_connected()) {
        printf("[OTA] ERROR: WiFi not connected!\n");
        text_mode_clear(TEXT_COLOR_BLACK);
        text_mode_print_at(0, 0, "Firmware Update");
        text_mode_print_at(0, 2, "WiFi not connected!");
        text_mode_flush();
        return;
    }

    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at(0, 0, "Firmware Update");
    text_mode_print_at(0, 2, "Releasing app heap...");
    text_mode_flush();

    // Release the app heap to free contiguous memory for TLS buffers.
    // This function is firmware code (OS stack + firmware BSS), so it is safe
    // to free the app heap here. We must not return to the calling app after this.
    app_heap_release();

    text_mode_print_at(0, 2, "Downloading...");
    text_mode_flush();

    mkdir("/sdcard/system", 0755);
    remove("/sdcard/system/firmware.tmp");

    const int max_stalled_retries = 4;
    int expected_total = 0;
    int downloaded_total = 0;
    int stalled_retries = 0;
    int successful_chunks = 0;
    esp_err_t final_err = ESP_FAIL;
    int final_status = 0;
    bool download_ok = false;

    while (stalled_retries < max_stalled_retries) {
        int before_attempt_total = downloaded_total;
        char status_line[40];

        FILE *fp = fopen("/sdcard/system/firmware.tmp", downloaded_total > 0 ? "ab" : "wb");
        if (!fp) {
            text_mode_print_at(0, 6, "Failed to create file!");
            text_mode_flush();
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }

        s_dl_ctx = (ota_dl_ctx_t){
            .fp = fp,
            .total = downloaded_total,
            .content_length = expected_total,
            .last_pct = -1,
            .error = false,
        };

        esp_http_client_config_t config = {
            .url = OTA_FIRMWARE_URL,
            .timeout_ms = 120000,
            .event_handler = ota_dl_event_handler,
            .user_data = &s_dl_ctx,
            .cert_pem = OTA_WE1_CA_PEM,
            .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .user_agent = "esposito-ota/1.0",
            .addr_type = HTTP_ADDR_TYPE_INET,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            fclose(fp);
            remove("/sdcard/system/firmware.tmp");
            text_mode_print_at(0, 6, "Failed to init HTTP!");
            text_mode_flush();
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }

        if (downloaded_total > 0) {
            char range_header[48];
            snprintf(range_header, sizeof(range_header), "bytes=%d-", downloaded_total);
            esp_http_client_set_header(client, "Range", range_header);
            printf("[OTA] Resume chunk %d (stalled retries: %d/%d) with %s\n",
                   successful_chunks + 1, stalled_retries, max_stalled_retries, range_header);
            snprintf(status_line, sizeof(status_line), "Chunk %d retry %d/%d",
                     successful_chunks + 1, stalled_retries, max_stalled_retries);
        } else {
            printf("[OTA] Download start (stalled retries: %d/%d)\n",
                   stalled_retries, max_stalled_retries);
            snprintf(status_line, sizeof(status_line), "Download start");
        }

        text_mode_print_at(0, 3, "                                        ");
        text_mode_print_at(0, 3, status_line);
        text_mode_flush();

        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);

        downloaded_total = s_dl_ctx.total;
        if (expected_total <= 0 && status == 200 && s_dl_ctx.content_length > 0) {
            expected_total = s_dl_ctx.content_length;
        }

        printf("[OTA] Perform result: %s (0x%x)\n", esp_err_to_name(err), err);
        printf("[OTA] HTTP status: %d\n", status);
        printf("[OTA] Downloaded: %d bytes, expected: %d, error flag: %d\n",
               downloaded_total, expected_total, s_dl_ctx.error);

        esp_http_client_cleanup(client);
        fclose(fp);

        final_err = err;
        final_status = status;

        if (err == ESP_OK && !s_dl_ctx.error && (status == 200 || status == 206)) {
            download_ok = true;
            break;
        }

        if (err == ESP_ERR_HTTP_INCOMPLETE_DATA && !s_dl_ctx.error &&
            (status == 200 || status == 206) && downloaded_total > 0) {
            if (downloaded_total > before_attempt_total) {
                successful_chunks++;
                stalled_retries = 0;
                printf("[OTA] Incomplete data but progress made (+%d bytes), continuing without consuming retries...\n",
                       downloaded_total - before_attempt_total);
                snprintf(status_line, sizeof(status_line), "Chunk ok +%d bytes", downloaded_total - before_attempt_total);
                text_mode_print_at(0, 5, "                                        ");
                text_mode_print_at(0, 5, status_line);
                text_mode_flush();
            } else {
                stalled_retries++;
                printf("[OTA] Incomplete data with no progress, consuming retry %d/%d...\n",
                       stalled_retries, max_stalled_retries);
                snprintf(status_line, sizeof(status_line), "Stalled retry %d/%d", stalled_retries, max_stalled_retries);
                text_mode_print_at(0, 5, "                                        ");
                text_mode_print_at(0, 5, status_line);
                text_mode_flush();
            }
            continue;
        }

        break;
    }

    printf("[OTA] Perform done: %s, status=%d, bytes=%d\n",
           esp_err_to_name(final_err), final_status, downloaded_total);

    if (!download_ok || (expected_total > 0 && downloaded_total < expected_total)) {
        remove("/sdcard/system/firmware.tmp");
        text_mode_print_at(0, 5, "Download failed");
        text_mode_print_at(0, 6, "Download error!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    int total = downloaded_total;
    printf("[OTA] Downloaded %d bytes\n", total);

    if (total <= 0) {
        remove("/sdcard/system/firmware.tmp");
        text_mode_print_at(0, 6, "Empty download!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    FILE *fp = fopen("/sdcard/system/firmware.tmp", "rb");
    if (!fp) {
        remove("/sdcard/system/firmware.tmp");
        text_mode_print_at(0, 6, "File read error!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    uint8_t magic;
    if (fread(&magic, 1, 1, fp) != 1) {
        fclose(fp);
        remove("/sdcard/system/firmware.tmp");
        text_mode_print_at(0, 6, "File read error!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    fclose(fp);

    if (magic != 0xE9) {
        remove("/sdcard/system/firmware.tmp");
        text_mode_print_at(0, 6, "Invalid firmware image!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    rename("/sdcard/system/firmware.tmp", "/sdcard/system/firmware.bin");

    draw_progress(100);
    text_mode_print_at(0, 6, "Rebooting to flash...");
    text_mode_flush();

    handoff_to_stub();
}
