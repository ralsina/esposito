// Note: some of config_get_string etc. use "size_t" return type
// but the app's code uses ssize_t in some places via C stdlib only.
// We match the firmware signature exactly.
#include "app_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CONFIG_DIR "/tmp/esposito_config"

static char current_app[64] = "";

static void ensure_dir(void) {
    mkdir(CONFIG_DIR, 0755);
}

static void get_path(const char *key, char *buf, size_t buf_size) {
    ensure_dir();
    if (current_app[0]) {
        snprintf(buf, buf_size, "%s/%s_%s", CONFIG_DIR, current_app, key ? key : "");
    } else {
        snprintf(buf, buf_size, "%s/%s", CONFIG_DIR, key ? key : "");
    }
}

bool config_bind_app(const char *app_name) {
    snprintf(current_app, sizeof(current_app), "%s", app_name);
    return true;
}

void config_unbind_app(void) {
    current_app[0] = '\0';
}

FILE *config_open_read(const char *key_path) {
    char path[512]; get_path(key_path, path, sizeof(path));
    return fopen(path, "r");
}

FILE *config_open_write(const char *key_path) {
    char path[512]; get_path(key_path, path, sizeof(path));
    return fopen(path, "w");
}

bool config_exists(const char *key_path) {
    char path[512]; get_path(key_path, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0;
}

bool config_delete(const char *key_path) {
    char path[512]; get_path(key_path, path, sizeof(path));
    return remove(path) == 0;
}

char *config_read_all_alloc(const char *key_path, size_t *out_len) {
    FILE *f = config_open_read(key_path);
    if (!f) { if (out_len) *out_len = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t)(len + 1));
    if (!data) { fclose(f); return NULL; }
    fread(data, 1, (size_t)len, f);
    data[len] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return data;
}

void config_free(void *ptr) { free(ptr); }
void appcfg_free(void *ptr) { free(ptr); }

int config_get_int(const char *key_path, int default_value) {
    FILE *f = config_open_read(key_path); if (!f) return default_value;
    int val; if (fscanf(f, "%d", &val) != 1) val = default_value;
    fclose(f); return val;
}

int appcfg_get_int(const char *key_path, int default_value) {
    return config_get_int(key_path, default_value);
}

float config_get_float(const char *key_path, float default_value) {
    FILE *f = config_open_read(key_path); if (!f) return default_value;
    float val; if (fscanf(f, "%f", &val) != 1) val = default_value;
    fclose(f); return val;
}

bool config_get_bool(const char *key_path, bool default_value) {
    FILE *f = config_open_read(key_path); if (!f) return default_value;
    int val; if (fscanf(f, "%d", &val) != 1) val = default_value ? 1 : 0;
    fclose(f); return val != 0;
}

bool appcfg_get_bool(const char *key_path, bool default_value) {
    return config_get_bool(key_path, default_value);
}

size_t config_get_string(const char *key_path, const char *default_value, char *out, size_t out_size) {
    FILE *f = config_open_read(key_path);
    if (!f) {
        if (out && out_size > 0) snprintf(out, out_size, "%s", default_value ? default_value : "");
        return 0;
    }
    size_t n = 0;
    if (fgets(out, (int)out_size, f)) {
        n = strlen(out);
        // Strip trailing newline
        if (n > 0 && out[n-1] == '\n') { out[n-1] = '\0'; n--; }
    }
    fclose(f); return n;
}

size_t appcfg_get_string(const char *key_path, const char *default_value, char *out, size_t out_size) {
    return config_get_string(key_path, default_value, out, out_size);
}

bool config_set_int(const char *key_path, int value) {
    FILE *f = config_open_write(key_path); if (!f) return false;
    fprintf(f, "%d", value); fclose(f); return true;
}

bool appcfg_set_int(const char *key_path, int value) {
    return config_set_int(key_path, value);
}

bool config_set_float(const char *key_path, float value) {
    FILE *f = config_open_write(key_path); if (!f) return false;
    fprintf(f, "%f", value); fclose(f); return true;
}

bool config_set_bool(const char *key_path, bool value) {
    FILE *f = config_open_write(key_path); if (!f) return false;
    fprintf(f, "%d", value ? 1 : 0); fclose(f); return true;
}

bool appcfg_set_bool(const char *key_path, bool value) {
    return config_set_bool(key_path, value);
}

bool config_set_string(const char *key_path, const char *value) {
    FILE *f = config_open_write(key_path); if (!f) return false;
    fprintf(f, "%s", value); fclose(f); return true;
}

bool appcfg_set_string(const char *key_path, const char *value) {
    return config_set_string(key_path, value);
}
