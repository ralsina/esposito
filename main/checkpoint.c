#include "checkpoint.h"
#include "app_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "checkpoint";

static bool checkpoint_opened = false;
static char *last_loaded_value = NULL;

bool checkpoint_open(const char *app_name) {
    if (!config_bind_app(app_name)) {
        ESP_LOGW(TAG, "Cannot bind config namespace for %s", app_name);
        checkpoint_opened = false;
        return false;
    }

    if (last_loaded_value) {
        config_free(last_loaded_value);
        last_loaded_value = NULL;
    }

    ESP_LOGI(TAG, "Checkpoint namespace ready for %s", app_name);
    checkpoint_opened = true;
    return true;
}

void checkpoint_save_string(const char *key, const char *value) {
    if (!checkpoint_opened) return;
    if (!config_set_string(key, value ? value : "")) {
        ESP_LOGW(TAG, "Failed to save string key %s", key);
        return;
    }
    ESP_LOGD(TAG, "Save string: %s", key);
}

const char *checkpoint_load_string(const char *key) {
    if (!checkpoint_opened) return NULL;

    if (last_loaded_value) {
        config_free(last_loaded_value);
        last_loaded_value = NULL;
    }

    size_t loaded_size = 0;
    last_loaded_value = config_read_all_alloc(key, &loaded_size);
    if (!last_loaded_value) {
        ESP_LOGD(TAG, "Load string: %s = (null)", key);
        return NULL;
    }

    ESP_LOGD(TAG, "Load string: %s (%u bytes)", key, (unsigned)loaded_size);
    return last_loaded_value;
}

void checkpoint_save_int(const char *key, int value) {
    if (!checkpoint_opened) return;
    if (!config_set_int(key, value)) {
        ESP_LOGW(TAG, "Failed to save int key %s", key);
        return;
    }
    ESP_LOGD(TAG, "Save int: %s = %d", key, value);
}

int checkpoint_load_int(const char *key) {
    if (!checkpoint_opened) return 0;
    return config_get_int(key, 0);
}

bool checkpoint_save(void) {
    return checkpoint_opened;
}

void checkpoint_close(void) {
    if (last_loaded_value) {
        config_free(last_loaded_value);
        last_loaded_value = NULL;
    }

    checkpoint_opened = false;
}
