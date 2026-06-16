#include "credential_store.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "cred_store";

// NVS namespace and keys. Keys are limited to 15 characters by NVS.
#define CRED_NVS_NAMESPACE "creds"
#define CRED_KEY_SSID      "ssid"
#define CRED_KEY_PASSWORD  "pass"

static bool cred_initialized = false;

bool credential_store_init(void) {
    if (cred_initialized) return true;

    // NVS flash is also initialized by wifi_init() at boot; calling here
    // is defensive (and idempotent in ESP-IDF if already initialized).
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, retrying...");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(ret));
            return false;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }
    cred_initialized = true;
    return true;
}

bool credential_store_get_ssid(char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    if (!credential_store_init()) return false;

    nvs_handle_t handle;
    if (nvs_open(CRED_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t ret = nvs_get_str(handle, CRED_KEY_SSID, out, &out_len);
    nvs_close(handle);
    if (ret != ESP_OK) {
        return false;
    }
    return out[0] != '\0';
}

bool credential_store_get_password(char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    if (!credential_store_init()) return false;

    nvs_handle_t handle;
    if (nvs_open(CRED_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t ret = nvs_get_str(handle, CRED_KEY_PASSWORD, out, &out_len);
    nvs_close(handle);
    return ret == ESP_OK;
}

bool credential_store_set(const char *ssid, const char *password) {
    if (!ssid || !ssid[0]) {
        ESP_LOGE(TAG, "Refusing to store empty SSID");
        return false;
    }
    if (!credential_store_init()) return false;

    nvs_handle_t handle;
    if (nvs_open(CRED_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for write failed");
        return false;
    }
    esp_err_t r_ssid = nvs_set_str(handle, CRED_KEY_SSID, ssid);
    esp_err_t r_pass = nvs_set_str(handle, CRED_KEY_PASSWORD, password ? password : "");
    esp_err_t r_commit = nvs_commit(handle);
    nvs_close(handle);

    if (r_ssid != ESP_OK || r_pass != ESP_OK || r_commit != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed: ssid=%s pass=%s commit=%s",
                 esp_err_to_name(r_ssid),
                 esp_err_to_name(r_pass),
                 esp_err_to_name(r_commit));
        return false;
    }
    return true;
}

void credential_store_erase(void) {
    if (!credential_store_init()) return;

    nvs_handle_t handle;
    if (nvs_open(CRED_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    // Erase is best-effort; ignore "not found" since the goal is "no creds stored".
    nvs_erase_key(handle, CRED_KEY_SSID);
    nvs_erase_key(handle, CRED_KEY_PASSWORD);
    nvs_commit(handle);
    nvs_close(handle);
}
