#include "wifi.h"
#include "credential_store.h"
#include "app_config.h"
#include "os_core.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "wifi";

static bool wifi_initialized = false;
static bool wifi_scanning = false;
static bool wifi_connected = false;
static bool ntp_initialized = false;
static bool time_synchronized = false;
static time_t last_time_sync = 0;
static int wifi_retry_count = 0;
static esp_timer_handle_t wifi_reconnect_timer;
static char wifi_ip[WIFI_IP_STR_LEN] = {0};
static char wifi_ssid[WIFI_MAX_SSID] = {0};
static char wifi_password[WIFI_MAX_PASSWORD] = {0};

// Scan results
static wifi_ap_record_t scan_results[WIFI_MAX_SCAN_RESULTS];
static int scan_count = 0;

// Legacy SD-card storage keys. Used only for one-shot migration to NVS.
// After migration, the trust model guarantees WiFi credentials live in NVS
// (see docs/trust-model.md) and these keys should never be re-populated.
#define WIFI_SETTINGS_SSID_KEY "wifi/ssid"
#define WIFI_SETTINGS_PASSWORD_KEY "wifi/password"

// One-shot migration: copy credentials from the old SD-card location to NVS,
// then physically delete the plaintext files from SD. Idempotent: if NVS
// already has credentials, this is a no-op.
//
// We use config_delete() (rather than os_settings_set_string("...","")) so the
// files are actually unlink()'d from the FAT filesystem rather than left as
// empty files revealing that this device once had WiFi configured.
static void migrate_credentials_from_sd_if_present(void) {
    char legacy_ssid[WIFI_MAX_SSID] = {0};
    char legacy_password[WIFI_MAX_PASSWORD] = {0};
    os_settings_get_string(WIFI_SETTINGS_SSID_KEY, "", legacy_ssid, sizeof(legacy_ssid));
    os_settings_get_string(WIFI_SETTINGS_PASSWORD_KEY, "", legacy_password, sizeof(legacy_password));

    if (legacy_ssid[0] == '\0') {
        return;  // nothing to migrate
    }

    ESP_LOGI(TAG, "Migrating WiFi credentials from SD card to NVS (SSID: %s)", legacy_ssid);
    if (credential_store_set(legacy_ssid, legacy_password)) {
        // Bind to the "settings" app namespace (must match OS_SETTINGS_APP_NAME
        // in os_core.c) to reach the legacy /sdcard/apps/settings/config/ files.
        bool deleted = false;
        if (config_bind_app("settings")) {
            bool del_ssid = config_delete(WIFI_SETTINGS_SSID_KEY);
            bool del_pass = config_delete(WIFI_SETTINGS_PASSWORD_KEY);
            config_unbind_app();
            deleted = del_ssid && del_pass;
        }
        if (deleted) {
            ESP_LOGI(TAG, "Migration complete; plaintext credentials removed from SD card");
        } else {
            // NVS now has the creds, but the SD plaintext files are still there.
            // The trust-model guarantee (creds live in NVS) holds for new code,
            // but the user should know the SD files weren't cleaned up.
            ESP_LOGW(TAG, "Migration copied to NVS, but failed to delete plaintext SD files");
        }
    } else {
        ESP_LOGE(TAG, "Migration to NVS failed; plaintext credentials remain on SD card");
    }

    // Scrub the local copies; the caller will re-read from NVS. Use secure_zero
    // rather than memset because the compiler's dead-store elimination can
    // otherwise elide the wipe (these buffers are not read again).
    credential_store_secure_zero(legacy_ssid, sizeof(legacy_ssid));
    credential_store_secure_zero(legacy_password, sizeof(legacy_password));
}

static void wifi_time_sync_notification(struct timeval *timeval_ptr) {
    (void)timeval_ptr;

    time_t now;
    time(&now);
    time_synchronized = true;
    last_time_sync = now;

    struct tm time_info;
    gmtime_r(&now, &time_info);

    char timestamp[32];
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", &time_info) > 0) {
        ESP_LOGI(TAG, "Time synchronized via NTP: %s", timestamp);
    } else {
        ESP_LOGI(TAG, "Time synchronized via NTP");
    }

    esp_sntp_stop();
    ntp_initialized = false;
    ESP_LOGI(TAG, "SNTP stopped after first sync to free memory");
}

static void wifi_start_time_sync(void) {
    if (!ntp_initialized) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.cloudflare.com");
        esp_sntp_set_time_sync_notification_cb(wifi_time_sync_notification);
        esp_sntp_init();
        ntp_initialized = true;
        ESP_LOGI(TAG, "NTP sync started");
        return;
    }

    esp_sntp_restart();
    ESP_LOGI(TAG, "NTP sync restarted after reconnect");
}

static void wifi_reconnect_timer_cb(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Reconnect timer fired, retrying WiFi connection");
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            if (!wifi_scanning) {
                esp_wifi_connect();
            }
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_connected = false;
            wifi_retry_count++;
            ESP_LOGW(TAG, "WiFi disconnected (retry %d)", wifi_retry_count);
            if (!wifi_scanning) {
                int delay_ms = (wifi_retry_count <= 5) ? 1000 :
                               (wifi_retry_count <= 15) ? 5000 : 30000;
                esp_timer_start_once(wifi_reconnect_timer, (uint64_t)delay_ms * 1000);
            }
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            wifi_connected = true;
            wifi_retry_count = 0;
            snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "WiFi connected, IP: %s", wifi_ip);
            wifi_start_time_sync();
        }
    }
}

static bool read_config(void) {
    // Preferred path: read from NVS.
    if (credential_store_get_ssid(wifi_ssid, sizeof(wifi_ssid))) {
        credential_store_get_password(wifi_password, sizeof(wifi_password));
        ESP_LOGI(TAG, "Read WiFi config from NVS for SSID: %s", wifi_ssid);
        return true;
    }

    // Migration path: if NVS is empty but the old SD-card files still hold
    // plaintext credentials, copy them to NVS and remove the SD files.
    migrate_credentials_from_sd_if_present();
    if (credential_store_get_ssid(wifi_ssid, sizeof(wifi_ssid))) {
        credential_store_get_password(wifi_password, sizeof(wifi_password));
        ESP_LOGI(TAG, "Read WiFi config from NVS after migration for SSID: %s", wifi_ssid);
        return true;
    }

    ESP_LOGI(TAG, "No WiFi config found");
    return false;
}

bool wifi_init(void) {
    if (wifi_initialized) return true;

    esp_timer_create_args_t timer_args = {
        .callback = wifi_reconnect_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_reconnect",
        .skip_unhandled_events = false
    };
    esp_timer_create(&timer_args, &wifi_reconnect_timer);

    // Initialize NVS (needed by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, retrying...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Set station mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_initialized = true;

    // Read config and connect
    if (read_config() && wifi_ssid[0] != '\0') {
        wifi_connect(wifi_ssid, wifi_password);
    }

    return true;
}

bool wifi_is_connected(void) {
    return wifi_connected;
}

const char *wifi_get_ip(void) {
    return wifi_ip;
}

int wifi_scan(void) {
    if (!wifi_initialized) return 0;

    wifi_scanning = true;

    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set STA mode failed: %s", esp_err_to_name(ret));
        wifi_scanning = false;
        return 0;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        wifi_scanning = false;
        return 0;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    wifi_scan_config_t scan_config;
    memset(&scan_config, 0, sizeof(scan_config));

    ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(ret));
        wifi_scanning = false;
        return 0;
    }

    uint16_t number = WIFI_MAX_SCAN_RESULTS;
    ret = esp_wifi_scan_get_ap_records(&number, scan_results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get scan results failed: %s", esp_err_to_name(ret));
        wifi_scanning = false;
        return 0;
    }

    scan_count = number;
    ESP_LOGI(TAG, "Scan found %d networks", scan_count);
    wifi_scanning = false;
    return scan_count;
}

const char *wifi_scan_get_ssid(int index) {
    if (index < 0 || index >= scan_count) return NULL;
    return (const char *)scan_results[index].ssid;
}

int wifi_scan_get_rssi(int index) {
    if (index < 0 || index >= scan_count) return 0;
    return scan_results[index].rssi;
}

bool wifi_connect(const char *ssid, const char *password) {
    if (!wifi_initialized) return false;

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && password[0] != '\0') {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set config failed: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_connected = false;
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(ret));
        return false;
    }

    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid) - 1);
    strncpy(wifi_password, password ? password : "", sizeof(wifi_password) - 1);
    return true;
}

void wifi_disconnect(void) {
    if (!wifi_initialized) return;
    esp_wifi_disconnect();
    wifi_connected = false;
    wifi_ip[0] = '\0';
}

bool wifi_time_is_synchronized(void) {
    return time_synchronized;
}

time_t wifi_time_last_sync(void) {
    return last_time_sync;
}

bool wifi_save_config(const char *ssid, const char *password) {
    if (!ssid || !ssid[0]) {
        ESP_LOGE(TAG, "Refusing to save empty WiFi SSID");
        return false;
    }

    if (!credential_store_set(ssid, password)) {
        ESP_LOGE(TAG, "Failed to persist WiFi config to NVS");
        return false;
    }

    ESP_LOGI(TAG, "WiFi config saved to NVS for SSID: %s", ssid);
    return true;
}
