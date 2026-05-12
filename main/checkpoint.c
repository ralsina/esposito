#include "checkpoint.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "checkpoint";

#define MAX_CHECKPOINT_SIZE 4096
static char checkpoint_data[MAX_CHECKPOINT_SIZE];
static bool checkpoint_opened = false;
static char checkpoint_path[64];

// Find a key in checkpoint_data. Returns pointer to value part, or NULL.
static const char *find_key(const char *key) {
    size_t klen = strlen(key);
    char *p = checkpoint_data;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            return p + klen + 1;
        }
        p += strlen(p) + 1;
    }
    return NULL;
}

// Return the total used bytes in checkpoint_data (including final double-null)
static size_t used_size(void) {
    char *p = checkpoint_data;
    size_t total = 0;
    while (*p) {
        total += strlen(p) + 1;
        p += strlen(p) + 1;
    }
    return total + 1;
}

// Replace or append a key=value entry
static void set_key(const char *key, const char *value) {
    size_t klen = strlen(key);
    size_t vlen = strlen(value);
    size_t entry_len = klen + 1 + vlen;

    char *p = checkpoint_data;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t old_len = strlen(p);
            long delta = (long)entry_len - (long)old_len;
            char *next = p + old_len + 1;
            size_t tail_bytes = used_size() - (next - checkpoint_data);

            if (delta > 0 && used_size() + delta > MAX_CHECKPOINT_SIZE) {
                ESP_LOGW(TAG, "Checkpoint buffer full");
                return;
            }

            if (delta != 0) {
                memmove(p + entry_len + 1, next, tail_bytes);
            }
            memcpy(p, key, klen);
            p[klen] = '=';
            memcpy(p + klen + 1, value, vlen);
            p[entry_len] = '\0';
            return;
        }
        p += strlen(p) + 1;
    }

    size_t used = p - checkpoint_data;
    if (used + entry_len + 2 > MAX_CHECKPOINT_SIZE) {
        ESP_LOGW(TAG, "Checkpoint buffer full");
        return;
    }
    memcpy(p, key, klen);
    p[klen] = '=';
    memcpy(p + klen + 1, value, vlen);
    p[entry_len] = '\0';
    p[entry_len + 1] = '\0';
}

bool checkpoint_open(const char *app_name) {
    snprintf(checkpoint_path, sizeof(checkpoint_path), "/sdcard/apps/%s/checkpoints", app_name);
    mkdir("/sdcard/apps", 0777);
    char app_dir[64];
    snprintf(app_dir, sizeof(app_dir), "/sdcard/apps/%s", app_name);
    mkdir(app_dir, 0777);
    checkpoint_data[0] = '\0';
    checkpoint_data[1] = '\0';

    FILE *fp = fopen(checkpoint_path, "r");
    if (fp) {
        size_t n = fread(checkpoint_data, 1, MAX_CHECKPOINT_SIZE - 2, fp);
        fclose(fp);
        if (n > 0 && checkpoint_data[n - 1] == '\n') n--;
        checkpoint_data[n] = '\0';
        // Convert newlines to null separators
        for (size_t i = 0; i < n; i++) {
            if (checkpoint_data[i] == '\n') checkpoint_data[i] = '\0';
        }
        ESP_LOGI(TAG, "Restored checkpoint for %s (%d bytes)", app_name, n);
    } else {
        ESP_LOGI(TAG, "No saved checkpoint for %s", app_name);
    }

    checkpoint_opened = true;
    return true;
}

void checkpoint_save_string(const char *key, const char *value) {
    if (!checkpoint_opened) return;
    set_key(key, value);
    ESP_LOGD(TAG, "Save string: %s = %s", key, value);
}

const char *checkpoint_load_string(const char *key) {
    if (!checkpoint_opened) return NULL;
    const char *val = find_key(key);
    ESP_LOGD(TAG, "Load string: %s = %s", key, val ? val : "(null)");
    return val;
}

void checkpoint_save_int(const char *key, int value) {
    if (!checkpoint_opened) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    set_key(key, buf);
    ESP_LOGD(TAG, "Save int: %s = %d", key, value);
}

int checkpoint_load_int(const char *key) {
    if (!checkpoint_opened) return 0;
    const char *val = find_key(key);
    if (!val) return 0;
    return atoi(val);
}

bool checkpoint_save(void) {
    if (!checkpoint_opened) return false;

    FILE *fp = fopen(checkpoint_path, "w");
    if (!fp) {
        ESP_LOGE(TAG, "Cannot write %s", checkpoint_path);
        return false;
    }

    // Write null-separated data as newline-separated
    char *p = checkpoint_data;
    while (*p) {
        fputs(p, fp);
        fputc('\n', fp);
        p += strlen(p) + 1;
    }

    fclose(fp);
    ESP_LOGI(TAG, "Checkpoint saved to %s", checkpoint_path);
    return true;
}

void checkpoint_close(void) {
    if (checkpoint_opened) {
        checkpoint_save();
    }
    checkpoint_opened = false;
    checkpoint_data[0] = '\0';
}
