#include "app_config.h"

#include "app_heap.h"
#include "esp_log.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char *TAG = "app_config";

#define CONFIG_BASE_DIR "/sdcard/apps"
#define CONFIG_MAX_PATH 512

static char bound_app[64];
static bool config_bound = false;

static bool mkdir_if_missing(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool ensure_parent_dirs(const char *file_path) {
    char tmp[CONFIG_MAX_PATH];
    size_t len = strnlen(file_path, sizeof(tmp) - 1);
    if (len == 0 || len >= sizeof(tmp)) {
        return false;
    }

    memcpy(tmp, file_path, len);
    tmp[len] = '\0';

    for (char *cursor = tmp + 1; *cursor; cursor++) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        if (!mkdir_if_missing(tmp)) {
            return false;
        }
        *cursor = '/';
    }
    return true;
}

static bool is_valid_segment_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.';
}

static bool validate_key_path(const char *key_path) {
    if (!key_path || !key_path[0]) {
        return false;
    }
    if (key_path[0] == '/') {
        return false;
    }

    bool in_segment = false;
    int segment_dots = 0;

    for (const char *cursor = key_path; ; cursor++) {
        char ch = *cursor;

        if (ch == '\0' || ch == '/') {
            if (!in_segment) {
                return false;
            }
            if (segment_dots == 1 || segment_dots == 2) {
                return false;
            }
            if (ch == '\0') {
                break;
            }
            in_segment = false;
            segment_dots = 0;
            continue;
        }

        if (!is_valid_segment_char(ch)) {
            return false;
        }

        if (!in_segment) {
            in_segment = true;
            segment_dots = 0;
        }
        if (ch == '.') {
            segment_dots++;
        } else {
            segment_dots = -100;
        }
    }

    return true;
}

static bool build_key_path(const char *key_path, char *out_path, size_t out_size) {
    if (!config_bound || !validate_key_path(key_path)) {
        return false;
    }

    int written = snprintf(out_path,
                           out_size,
                           CONFIG_BASE_DIR "/%s/config/%s",
                           bound_app,
                           key_path);
    if (written <= 0 || (size_t)written >= out_size) {
        return false;
    }
    return true;
}

bool config_bind_app(const char *app_name) {
    if (!app_name || !app_name[0]) {
        return false;
    }

    strncpy(bound_app, app_name, sizeof(bound_app) - 1);
    bound_app[sizeof(bound_app) - 1] = '\0';
    config_bound = true;

    char app_dir[CONFIG_MAX_PATH];
    char cfg_dir[CONFIG_MAX_PATH];

    int app_len = snprintf(app_dir, sizeof(app_dir), CONFIG_BASE_DIR "/%s", bound_app);
    int cfg_len = snprintf(cfg_dir, sizeof(cfg_dir), CONFIG_BASE_DIR "/%s/config", bound_app);
    if (app_len <= 0 || cfg_len <= 0 || (size_t)app_len >= sizeof(app_dir) || (size_t)cfg_len >= sizeof(cfg_dir)) {
        config_bound = false;
        bound_app[0] = '\0';
        return false;
    }

    if (!mkdir_if_missing(CONFIG_BASE_DIR) || !mkdir_if_missing(app_dir) || !mkdir_if_missing(cfg_dir)) {
        ESP_LOGE(TAG, "Failed to create config dirs for app %s", bound_app);
        config_bound = false;
        bound_app[0] = '\0';
        return false;
    }

    return true;
}

void config_unbind_app(void) {
    config_bound = false;
    bound_app[0] = '\0';
}

FILE *config_open_read(const char *key_path) {
    char path[CONFIG_MAX_PATH];
    if (!build_key_path(key_path, path, sizeof(path))) {
        return NULL;
    }
    return fopen(path, "rb");
}

FILE *config_open_write(const char *key_path) {
    char path[CONFIG_MAX_PATH];
    if (!build_key_path(key_path, path, sizeof(path))) {
        return NULL;
    }

    if (!ensure_parent_dirs(path)) {
        return NULL;
    }
    return fopen(path, "wb");
}

bool config_exists(const char *key_path) {
    char path[CONFIG_MAX_PATH];
    struct stat st;

    if (!build_key_path(key_path, path, sizeof(path))) {
        return false;
    }
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool config_delete(const char *key_path) {
    char path[CONFIG_MAX_PATH];

    if (!build_key_path(key_path, path, sizeof(path))) {
        return false;
    }
    if (unlink(path) == 0) {
        return true;
    }
    return errno == ENOENT;
}

char *config_read_all_alloc(const char *key_path, size_t *out_len) {
    FILE *fp = config_open_read(key_path);
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buffer = app_malloc((size_t)size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    if (nread != (size_t)size) {
        app_free(buffer);
        return NULL;
    }

    buffer[nread] = '\0';
    if (out_len) {
        *out_len = nread;
    }
    return buffer;
}

void config_free(void *ptr) {
    app_free(ptr);
}

static bool read_small_text(const char *key_path, char *buffer, size_t buffer_size) {
    FILE *fp = config_open_read(key_path);
    if (!fp || buffer_size == 0) {
        if (fp) fclose(fp);
        return false;
    }

    size_t nread = fread(buffer, 1, buffer_size - 1, fp);
    fclose(fp);
    buffer[nread] = '\0';

    while (nread > 0 && (buffer[nread - 1] == '\n' || buffer[nread - 1] == '\r' || buffer[nread - 1] == ' ' || buffer[nread - 1] == '\t')) {
        buffer[nread - 1] = '\0';
        nread--;
    }

    return true;
}

int config_get_int(const char *key_path, int default_value) {
    char buffer[32];
    char *end_ptr = NULL;

    if (!read_small_text(key_path, buffer, sizeof(buffer))) {
        return default_value;
    }

    long value = strtol(buffer, &end_ptr, 10);
    if (end_ptr == buffer || *end_ptr != '\0') {
        return default_value;
    }
    return (int)value;
}

float config_get_float(const char *key_path, float default_value) {
    char buffer[64];
    char *end_ptr = NULL;

    if (!read_small_text(key_path, buffer, sizeof(buffer))) {
        return default_value;
    }

    float value = strtof(buffer, &end_ptr);
    if (end_ptr == buffer || *end_ptr != '\0') {
        return default_value;
    }
    return value;
}

bool config_get_bool(const char *key_path, bool default_value) {
    char buffer[16];

    if (!read_small_text(key_path, buffer, sizeof(buffer))) {
        return default_value;
    }

    if (strcmp(buffer, "1") == 0 || strcasecmp(buffer, "true") == 0 || strcasecmp(buffer, "yes") == 0 || strcasecmp(buffer, "on") == 0) {
        return true;
    }
    if (strcmp(buffer, "0") == 0 || strcasecmp(buffer, "false") == 0 || strcasecmp(buffer, "no") == 0 || strcasecmp(buffer, "off") == 0) {
        return false;
    }
    return default_value;
}

size_t config_get_string(const char *key_path,
                         const char *default_value,
                         char *out,
                         size_t out_size) {
    if (!out || out_size == 0) {
        return 0;
    }

    FILE *fp = config_open_read(key_path);
    if (!fp) {
        const char *fallback = default_value ? default_value : "";
        strncpy(out, fallback, out_size - 1);
        out[out_size - 1] = '\0';
        return strlen(out);
    }

    size_t nread = fread(out, 1, out_size - 1, fp);
    fclose(fp);
    out[nread] = '\0';
    return nread;
}

bool config_set_string(const char *key_path, const char *value) {
    FILE *fp = config_open_write(key_path);
    if (!fp) {
        return false;
    }

    const char *to_write = value ? value : "";
    size_t len = strlen(to_write);
    bool ok = fwrite(to_write, 1, len, fp) == len;
    fclose(fp);
    return ok;
}

bool config_set_int(const char *key_path, int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return config_set_string(key_path, buffer);
}

bool config_set_float(const char *key_path, float value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.9g", value);
    return config_set_string(key_path, buffer);
}

bool config_set_bool(const char *key_path, bool value) {
    return config_set_string(key_path, value ? "true" : "false");
}