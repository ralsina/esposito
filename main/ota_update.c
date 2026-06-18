#include "ota_update.h"
#include "ota_keys.h"
#include "text_mode.h"
#include "app_heap.h"
#include "os_core.h"
#include "hardware.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core_json.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>  // atoi
#include <sys/stat.h>

extern bool wifi_is_connected(void);

static const char *TAG = "ota";

// --- Cached release info from the most recent ota_check_for_update ---
// Used by ota_apply_update to know what to download.
static char cached_tag[32] = {0};
static char cached_firmware_url[256] = {0};
static char cached_sig_url[256] = {0};

const char *ota_firmware_version(void) {
    return FIRMWARE_VERSION;
}

// --- HTTP helpers ---
// All OTA HTTP traffic uses the ESP-IDF Mozilla certificate bundle rather
// than a per-host pinned PEM. GitHub's CDN chain is well-rooted; the bundle
// already shipped in firmware for the OS HTTP API.

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

// Fetch a small JSON/text payload into a buffer. Optionally set an Accept
// header (NULL to skip). Returns bytes written (excluding NUL), or negative
// on failure (transport) or negative-of-status (HTTP error).
static int ota_http_get(const char *url, const char *accept_header,
                         char *out, size_t out_size, int timeout_ms) {
    if (!url || !out || out_size < 2) return -1;
    out[0] = '\0';

    ota_http_ctx_t ctx = { .buf = out, .buf_size = out_size, .len = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 10000,
        .event_handler = ota_http_event_handler,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
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
    if (accept_header) {
        esp_http_client_set_header(client, "Accept", accept_header);
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

// --- GitHub releases JSON parsing ---

// Extract tag_name and the firmware.bin / firmware.bin.sig asset URLs from a
// GitHub releases API response. Handles both /releases/latest (single object)
// and /releases (array -- caller passes a pointer to the FIRST object).
//
// coreJSON does not support filter expressions, so assets[] is iterated by
// index until both URLs are found (or up to a sane cap).
static bool parse_release_json(char *json, size_t json_len,
                                char *tag, size_t tag_size,
                                char *fw_url, size_t fw_url_size,
                                char *sig_url, size_t sig_url_size) {
    if (!json || json_len == 0) return false;
    tag[0] = fw_url[0] = sig_url[0] = '\0';

    // tag_name
    const char *val = NULL;
    size_t vlen = 0;
    static const char tag_q[] = "tag_name";
    if (JSON_SearchConst(json, json_len, tag_q, sizeof(tag_q) - 1, &val, &vlen, NULL) != JSONSuccess) {
        ESP_LOGE(TAG, "tag_name not found in release JSON");
        return false;
    }
    if (vlen >= 2 && val[0] == '"') { val++; vlen -= 2; }
    size_t copy = vlen < tag_size - 1 ? vlen : tag_size - 1;
    memcpy(tag, val, copy);
    tag[copy] = '\0';

    // assets[].name + browser_download_url -- iterate up to 8 assets.
    for (int i = 0; i < 8; i++) {
        char query[64];
        const char *name_val = NULL;
        size_t name_len = 0;
        snprintf(query, sizeof(query), "assets[%d].name", i);
        JSONStatus_t s = JSON_SearchConst(json, json_len, query, strlen(query),
                              &name_val, &name_len, NULL);
        ESP_LOGI(TAG, "asset[%d] name search: %d", i, s);
        if (s != JSONSuccess) {
            ESP_LOGI(TAG, "no more assets at index %d", i);
            break;  // no more assets
        }

        const char *url_val = NULL;
        size_t url_len = 0;
        snprintf(query, sizeof(query), "assets[%d].browser_download_url", i);
        if (JSON_SearchConst(json, json_len, query, strlen(query),
                              &url_val, &url_len, NULL) != JSONSuccess) {
            ESP_LOGW(TAG, "asset[%d] has name but no download URL", i);
            continue;
        }

        ESP_LOGI(TAG, "asset[%d]: %.*s", i, (int)name_len, name_val);

        if (name_len >= 2 && name_val[0] == '"') {
            name_val++;
            name_len -= 2;
        }

        // Match asset name (length-checked to avoid strncasecmp weirdness on non-NUL data).
        static const char fw_name[] = "firmware.bin";
        static const char sig_name[] = "firmware.bin.sig";
        // Match the longest candidate first so "firmware.bin.sig" isn't
        // accidentally caught by a "firmware.bin" prefix check.
        if (name_len == sizeof(sig_name) - 1 && strncmp(name_val, sig_name, name_len) == 0) {
            if (url_len >= 2 && url_val[0] == '"') { url_val++; url_len -= 2; }
            size_t c = url_len < sig_url_size - 1 ? url_len : sig_url_size - 1;
            memcpy(sig_url, url_val, c);
            sig_url[c] = '\0';
        } else if (name_len == sizeof(fw_name) - 1 && strncmp(name_val, fw_name, name_len) == 0) {
            if (url_len >= 2 && url_val[0] == '"') { url_val++; url_len -= 2; }
            size_t c = url_len < fw_url_size - 1 ? url_len : fw_url_size - 1;
            memcpy(fw_url, url_val, c);
            fw_url[c] = '\0';
        }
    }

    if (fw_url[0] == '\0' || sig_url[0] == '\0') {
        ESP_LOGE(TAG, "Release %s is missing firmware.bin or firmware.bin.sig asset", tag);
        return false;
    }
    return true;
}

// --- Semver comparison ---

// Parse leading "v1.2.3" (with optional 'v'/'V' prefix) from s.
// major/minor/patch receive the parsed numbers (0 on parse failure).
// Returns pointer to the first character after patch (typically '-' for a
// pre-release suffix, '+' for build metadata, '\0' for end, etc.).
static const char *parse_semver(const char *s, int *major, int *minor, int *patch) {
    *major = *minor = *patch = 0;
    if (!s) return NULL;
    if (*s == 'v' || *s == 'V') s++;

    *major = atoi(s);
    while (*s && *s != '.' && *s != '-' && *s != '+') s++;
    if (*s == '.') s++;

    *minor = atoi(s);
    while (*s && *s != '.' && *s != '-' && *s != '+') s++;
    if (*s == '.') s++;

    *patch = atoi(s);
    while (*s && *s != '-' && *s != '+') s++;
    return s;
}

// Compare two version strings per semver rules.
// Returns: 1 if a > b, -1 if a < b, 0 if equal.
// A version with a pre-release suffix (e.g. "v1.0.0-beta.1") is LOWER than
// the same version without one (per semver spec).
static int compare_versions(const char *a, const char *b) {
    int amaj, amin, apat, bmaj, bmin, bpat;
    const char *a_rest = parse_semver(a, &amaj, &amin, &apat);
    const char *b_rest = parse_semver(b, &bmaj, &bmin, &bpat);

    if (amaj != bmaj) return amaj < bmaj ? -1 : 1;
    if (amin != bmin) return amin < bmin ? -1 : 1;
    if (apat != bpat) return apat < bpat ? -1 : 1;

    // Equal X.Y.Z. Pre-release has lower precedence than no pre-release.
    bool a_pre = a_rest && *a_rest == '-';
    bool b_pre = b_rest && *b_rest == '-';
    if (a_pre && !b_pre) return -1;
    if (!a_pre && b_pre) return 1;
    if (a_pre && b_pre && a_rest && b_rest) {
        // Simple string compare of the pre-release tag (incl. leading '-').
        int r = strcmp(a_rest, b_rest);
        return r < 0 ? -1 : (r > 0 ? 1 : 0);
    }
    return 0;
}

// --- UI helpers ---

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

    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%3d%%", pct < 100 ? pct : 100);
    text_mode_print_at(2 + (bar_width - 3) / 2, bar_y, pct_str);
    text_mode_flush();
}

// --- Stub handoff (unchanged from previous implementation) ---

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

// --- SD-card recovery check (unchanged: unsigned by design) ---

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

// --- Signature verification ---

// Compute SHA-256 of the file at the given path, returning the 32-byte digest
// in out_hash. Returns true on success.
static bool compute_file_sha256(const char *path, uint8_t out_hash[32]) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Cannot open %s for SHA-256", path);
        return false;
    }

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        ESP_LOGE(TAG, "mbedtls_md_info_from_type(SHA256) returned NULL");
        fclose(fp);
        return false;
    }

    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    int rc = mbedtls_md_setup(&md, info, 0);  // 0 = no HMAC
    if (rc != 0) {
        ESP_LOGE(TAG, "mbedtls_md_setup failed: -0x%x", -rc);
        mbedtls_md_free(&md);
        fclose(fp);
        return false;
    }
    rc = mbedtls_md_starts(&md);
    if (rc != 0) {
        ESP_LOGE(TAG, "mbedtls_md_starts failed: -0x%x", -rc);
        mbedtls_md_free(&md);
        fclose(fp);
        return false;
    }

    uint8_t buf[4096];
    size_t total = 0;
    while (true) {
        size_t got = fread(buf, 1, sizeof(buf), fp);
        if (got == 0) break;
        rc = mbedtls_md_update(&md, buf, got);
        if (rc != 0) {
            ESP_LOGE(TAG, "mbedtls_md_update failed: -0x%x", -rc);
            mbedtls_md_free(&md);
            fclose(fp);
            return false;
        }
        total += got;
    }
    fclose(fp);

    rc = mbedtls_md_finish(&md, out_hash);
    mbedtls_md_free(&md);
    if (rc != 0) {
        ESP_LOGE(TAG, "mbedtls_md_finish failed: -0x%x", -rc);
        return false;
    }
    ESP_LOGI(TAG, "SHA-256 of %s (%u bytes) computed", path, (unsigned)total);
    return true;
}

// Verify a DER-encoded ECDSA-P256 signature against the embedded public key.
// hash must be a 32-byte SHA-256 digest. sig/signature_len is the DER signature.
// Returns true iff the signature is valid.
static bool verify_signature(const uint8_t hash[32], const uint8_t *signature, size_t signature_len) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int rc = mbedtls_pk_parse_public_key(&pk,
                                          (const unsigned char *)ota_release_public_key_pem,
                                          sizeof(ota_release_public_key_pem));
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to parse embedded public key: -0x%x", -rc);
        mbedtls_pk_free(&pk);
        return false;
    }

    rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, signature, signature_len);
    mbedtls_pk_free(&pk);

    if (rc != 0) {
        ESP_LOGE(TAG, "Signature verification FAILED: -0x%x", -rc);
        return false;
    }
    ESP_LOGI(TAG, "Signature verification OK");
    return true;
}

// --- Public API: check for updates ---

bool ota_check_for_update(char *latest_version, size_t max_len) {
    if (!latest_version || max_len < 1) return false;
    latest_version[0] = '\0';
    cached_tag[0] = cached_firmware_url[0] = cached_sig_url[0] = '\0';

    // Pick endpoint based on channel setting.
    bool beta = (os_settings_get_int(OTA_BETA_CHANNEL_KEY, 0) == 1);
    const char *api_url = beta ? OTA_RELEASES_API_BETA : OTA_RELEASES_API_STABLE;

    // GitHub release JSON can be 10-20 KB with release notes and author info.
    size_t json_buf_size = 20480;
    char *json_buf = malloc(json_buf_size);
    if (!json_buf) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for GitHub API response", (unsigned)json_buf_size);
        return false;
    }
    int total = ota_http_get(api_url, "application/vnd.github+json",
                              json_buf, json_buf_size, 15000);
    if (total <= 0) {
        ESP_LOGE(TAG, "GitHub releases API request failed: %d", total);
        free(json_buf);
        return false;
    }

    ESP_LOGI(TAG, "GitHub API response: %d bytes", total);
    ESP_LOGI(TAG, "Response start: %.200s", json_buf);

    // For the beta endpoint the response is a JSON array. parse_release_json
    // expects a single object; seek to the first '{' to skip the array opener.
    char *json_start = json_buf;
    if (beta) {
        json_start = strchr(json_buf, '{');
        if (!json_start) {
            ESP_LOGE(TAG, "Malformed beta-channel response (no object)");
            free(json_buf);
            return false;
        }
    }

    char tag[32] = {0}, fw_url[256] = {0}, sig_url[256] = {0};
    if (!parse_release_json(json_start, json_buf + total - json_start,
                             tag, sizeof(tag),
                             fw_url, sizeof(fw_url),
                             sig_url, sizeof(sig_url))) {
        ESP_LOGE(TAG, "Failed to parse release JSON");
        free(json_buf);
        return false;
    }

    ESP_LOGI(TAG, "Latest release: %s", tag);
    printf("[OTA] Current: %s, Latest: %s\n", FIRMWARE_VERSION, tag);

    // Rollback protection: refuse to downgrade over the network.
    // Same-version updates are allowed (e.g. dirty build → clean release).
    int cmp = compare_versions(tag, FIRMWARE_VERSION);
    if (cmp < 0) {
        ESP_LOGI(TAG, "Release %s is not newer than running %s; refusing OTA (use SD-card path to downgrade)",
                 tag, FIRMWARE_VERSION);
        free(json_buf);
        return false;
    }

    // Cache for the apply step.
    snprintf(cached_tag, sizeof(cached_tag), "%s", tag);
    snprintf(cached_firmware_url, sizeof(cached_firmware_url), "%s", fw_url);
    snprintf(cached_sig_url, sizeof(cached_sig_url), "%s", sig_url);

    if (strlen(tag) >= max_len) { free(json_buf); return false; }
    strcpy(latest_version, tag);
    free(json_buf);
    return true;
}

// --- Download helper with resume (firmware.bin to SD) ---

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
            ESP_LOGI(TAG, "Content-Length: %d", s_dl_ctx.content_length);
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

// Download url to /sdcard/system/<dest_filename> with HTTP Range resume.
// Returns true on success. Same retry/stall logic as before.
static bool download_with_resume(const char *url, const char *dest_filename) {
    char dest_path[64];
    snprintf(dest_path, sizeof(dest_path), "/sdcard/system/%s", dest_filename);
    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "/sdcard/system/%s.tmp", dest_filename);

    mkdir("/sdcard/system", 0755);
    remove(tmp_path);

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

        FILE *fp = fopen(tmp_path, downloaded_total > 0 ? "ab" : "wb");
        if (!fp) {
            ESP_LOGE(TAG, "Failed to open %s for write", tmp_path);
            return false;
        }

        s_dl_ctx = (ota_dl_ctx_t){
            .fp = fp,
            .total = downloaded_total,
            .content_length = expected_total,
            .last_pct = -1,
            .error = false,
        };

        esp_http_client_config_t config = {
            .url = url,
            .timeout_ms = 120000,
            .event_handler = ota_dl_event_handler,
            .user_data = &s_dl_ctx,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .user_agent = "esposito-ota/1.0",
            .addr_type = HTTP_ADDR_TYPE_INET,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            fclose(fp);
            remove(tmp_path);
            ESP_LOGE(TAG, "Failed to init HTTP client");
            return false;
        }

        if (downloaded_total > 0) {
            char range_header[48];
            snprintf(range_header, sizeof(range_header), "bytes=%d-", downloaded_total);
            esp_http_client_set_header(client, "Range", range_header);
            ESP_LOGI(TAG, "Resume chunk %d (stalled retries: %d/%d) with %s",
                     successful_chunks + 1, stalled_retries, max_stalled_retries, range_header);
            snprintf(status_line, sizeof(status_line), "Chunk %d retry %d/%d",
                     successful_chunks + 1, stalled_retries, max_stalled_retries);
        } else {
            ESP_LOGI(TAG, "Download start (stalled retries: %d/%d)", stalled_retries, max_stalled_retries);
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

        ESP_LOGI(TAG, "Perform: %s (0x%x), HTTP %d, downloaded=%d expected=%d err_flag=%d",
                 esp_err_to_name(err), err, status, downloaded_total, expected_total, s_dl_ctx.error);

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
                ESP_LOGI(TAG, "Incomplete data but progress made (+%d bytes), continuing",
                         downloaded_total - before_attempt_total);
                snprintf(status_line, sizeof(status_line), "Chunk ok +%d bytes", downloaded_total - before_attempt_total);
                text_mode_print_at(0, 5, "                                        ");
                text_mode_print_at(0, 5, status_line);
                text_mode_flush();
            } else {
                stalled_retries++;
                ESP_LOGW(TAG, "Stalled, consuming retry %d/%d", stalled_retries, max_stalled_retries);
                snprintf(status_line, sizeof(status_line), "Stalled retry %d/%d", stalled_retries, max_stalled_retries);
                text_mode_print_at(0, 5, "                                        ");
                text_mode_print_at(0, 5, status_line);
                text_mode_flush();
            }
            continue;
        }

        break;
    }

    ESP_LOGI(TAG, "Download done: %s, status=%d, bytes=%d",
             esp_err_to_name(final_err), final_status, downloaded_total);

    if (!download_ok || (expected_total > 0 && downloaded_total < expected_total) || downloaded_total <= 0) {
        remove(tmp_path);
        return false;
    }

    if (rename(tmp_path, dest_path) != 0) {
        ESP_LOGE(TAG, "Failed to rename %s -> %s", tmp_path, dest_path);
        remove(tmp_path);
        return false;
    }
    return true;
}

// --- Public API: apply update ---

const char *ota_apply_update(void) {
    ESP_LOGI(TAG, "Starting OTA update");

    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected");
        return "WiFi not connected";
    }

    // If the cache is empty (e.g. device rebooted between check and apply),
    // re-query so we still know what to download.
    if (cached_firmware_url[0] == '\0') {
        char tag[32];
        if (!ota_check_for_update(tag, sizeof(tag))) {
            ESP_LOGE(TAG, "OTA aborted: no update available (check failed)");
            return "No update available";
        }
    }

    ESP_LOGI(TAG, "OTA proceeding: tag=%s url=%s", cached_tag, cached_firmware_url);

    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at(0, 0, "Firmware Update");

    char current_tag[64];
    snprintf(current_tag, sizeof(current_tag), "Update: %s", cached_tag[0] ? cached_tag : "?");
    text_mode_print_at(0, 1, current_tag);
    text_mode_print_at(0, 2, "Releasing app heap...");
    text_mode_flush();

    // Release the app heap to free contiguous memory for TLS buffers.
    // This is firmware code (OS stack + firmware BSS), so it is safe to free
    // the app heap here. We must not return to the calling app after this.
    app_heap_release();

    // 1) Download firmware.bin (with resume).
    text_mode_print_at(0, 2, "Downloading firmware...   ");
    text_mode_flush();
    if (!download_with_resume(cached_firmware_url, "firmware.bin")) {
        ESP_LOGE(TAG, "firmware.bin download failed");
        text_mode_print_at(0, 5, "Download failed");
        text_mode_print_at(0, 6, "Firmware download error!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    // 2) Download firmware.bin.sig (small, single request).
    text_mode_print_at(0, 2, "Downloading signature...  ");
    text_mode_flush();
    if (!download_with_resume(cached_sig_url, "firmware.bin.sig")) {
        ESP_LOGE(TAG, "firmware.bin.sig download failed");
        text_mode_print_at(0, 5, "Signature download failed");
        text_mode_print_at(0, 6, "Missing signature!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    // 3) Verify signature.
    text_mode_print_at(0, 2, "Verifying signature...    ");
    text_mode_flush();

    uint8_t hash[32];
    if (!compute_file_sha256("/sdcard/system/firmware.bin", hash)) {
        text_mode_print_at(0, 6, "Hash error!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    // Load signature file. DER ECDSA-P256 sig is typically 70-72 bytes;
    // allow up to 256 for headroom.
    FILE *sig_fp = fopen("/sdcard/system/firmware.bin.sig", "rb");
    if (!sig_fp) {
        text_mode_print_at(0, 6, "Cannot read signature!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    uint8_t sig_buf[256];
    size_t sig_len = fread(sig_buf, 1, sizeof(sig_buf), sig_fp);
    fclose(sig_fp);
    if (sig_len == 0 || sig_len >= sizeof(sig_buf)) {
        ESP_LOGE(TAG, "Signature file size out of range: %u", (unsigned)sig_len);
        text_mode_print_at(0, 6, "Bad signature file!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    if (!verify_signature(hash, sig_buf, sig_len)) {
        ESP_LOGE(TAG, "REJECTING firmware: signature verification failed");
        // Scrub the unsigned/incompatible firmware so we don't accidentally
        // install it on the next reboot via the SD-card recovery path.
        remove("/sdcard/system/firmware.bin");
        remove("/sdcard/system/firmware.bin.sig");
        text_mode_print_at(0, 5, "Signature INVALID");
        text_mode_print_at(0, 6, "Refusing unsigned firmware!");
        text_mode_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    ESP_LOGI(TAG, "Firmware signature verified, applying update");
    draw_progress(100);
    text_mode_print_at(0, 6, "Rebooting to flash...");
    text_mode_flush();

    handoff_to_stub();
    return NULL;
}
