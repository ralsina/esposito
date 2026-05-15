#include "wifi.h"
#include "os_core.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "wifi";

static bool wifi_initialized = false;
static bool wifi_connected = false;
static bool ntp_initialized = false;
static bool time_synchronized = false;
static time_t last_time_sync = 0;
static char wifi_ip[WIFI_IP_STR_LEN] = {0};
static char wifi_ssid[WIFI_MAX_SSID] = {0};
static char wifi_password[WIFI_MAX_PASSWORD] = {0};

// Scan results
static wifi_ap_record_t scan_results[WIFI_MAX_SCAN_RESULTS];
static int scan_count = 0;

#define WIFI_SETTINGS_SSID_KEY "wifi/ssid"
#define WIFI_SETTINGS_PASSWORD_KEY "wifi/password"

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_connected = false;
            ESP_LOGI(TAG, "WiFi disconnected");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            wifi_connected = true;
            snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "WiFi connected, IP: %s", wifi_ip);
            wifi_start_time_sync();
        }
    }
}

static bool read_config(void) {
    os_settings_get_string(WIFI_SETTINGS_SSID_KEY, "", wifi_ssid, sizeof(wifi_ssid));
    os_settings_get_string(WIFI_SETTINGS_PASSWORD_KEY, "", wifi_password, sizeof(wifi_password));

    if (wifi_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Read WiFi config from shared settings for SSID: %s", wifi_ssid);
        return true;
    }

    ESP_LOGI(TAG, "No WiFi config found in shared settings");
    return false;
}

bool wifi_init(void) {
    if (wifi_initialized) return true;

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

    wifi_scan_config_t scan_config;
    memset(&scan_config, 0, sizeof(scan_config));

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(ret));
        return 0;
    }

    uint16_t number = WIFI_MAX_SCAN_RESULTS;
    ret = esp_wifi_scan_get_ap_records(&number, scan_results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get scan results failed: %s", esp_err_to_name(ret));
        return 0;
    }

    scan_count = number;
    ESP_LOGI(TAG, "Scan found %d networks", scan_count);
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

    bool saved_ssid = os_settings_set_string(WIFI_SETTINGS_SSID_KEY, ssid);
    bool saved_password = os_settings_set_string(WIFI_SETTINGS_PASSWORD_KEY, password ? password : "");
    if (!saved_ssid || !saved_password) {
        ESP_LOGE(TAG, "Failed to persist WiFi config to shared settings");
        return false;
    }

    ESP_LOGI(TAG, "WiFi config saved for SSID: %s", ssid);
    return true;
}
