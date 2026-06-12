#include "app_manifest.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "app_manifest";

// Case-insensitive comparison of the first n chars of a and b.
static int str_tolower_eq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

// Check whether ext (no leading dot, e.g. "jpg") is listed in manifest's
// comma-separated extensions string (e.g. "jpg,jpeg").
static bool manifest_has_extension(const app_sd_manifest_t *m, const char *ext) {
    if (!m->extensions[0]) return false;
    size_t ext_len = strlen(ext);
    const char *p = m->extensions;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t token_len = comma ? (size_t)(comma - p) : strlen(p);
        if (ext_len == token_len && str_tolower_eq_n(ext, p, token_len)) {
            return true;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return false;
}

bool app_manifest_read(const char *app_name, app_sd_manifest_t *out) {
    if (!app_name || !out) return false;

    // Fill defaults
    strncpy(out->display_name, app_name, APP_MANIFEST_NAME_MAX - 1);
    out->display_name[APP_MANIFEST_NAME_MAX - 1] = '\0';
    out->short_description[0] = '\0';
    out->long_description[0] = '\0';
    out->homepage[0] = '\0';
    out->version[0] = '\0';
    out->extensions[0] = '\0';
    out->requires[0] = '\0';
    out->show_in_launcher = true;

    char path[192];
    snprintf(path, sizeof(path), "/sdcard/apps/%s/manifest.cfg", app_name);
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline / carriage return
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        if (strcmp(key, "name") == 0) {
            strncpy(out->display_name, value, APP_MANIFEST_NAME_MAX - 1);
            out->display_name[APP_MANIFEST_NAME_MAX - 1] = '\0';
        } else if (strcmp(key, "extensions") == 0) {
            strncpy(out->extensions, value, APP_MANIFEST_EXT_MAX - 1);
            out->extensions[APP_MANIFEST_EXT_MAX - 1] = '\0';
        } else if (strcmp(key, "short_description") == 0) {
            strncpy(out->short_description, value, APP_MANIFEST_DESC_MAX - 1);
            out->short_description[APP_MANIFEST_DESC_MAX - 1] = '\0';
        } else if (strcmp(key, "description") == 0) {
            strncpy(out->short_description, value, APP_MANIFEST_DESC_MAX - 1);
            out->short_description[APP_MANIFEST_DESC_MAX - 1] = '\0';
        } else if (strcmp(key, "long_description") == 0) {
            strncpy(out->long_description, value, APP_MANIFEST_LDESC_MAX - 1);
            out->long_description[APP_MANIFEST_LDESC_MAX - 1] = '\0';
        } else if (strcmp(key, "homepage") == 0) {
            strncpy(out->homepage, value, APP_MANIFEST_NAME_MAX - 1);
            out->homepage[APP_MANIFEST_NAME_MAX - 1] = '\0';
        } else if (strcmp(key, "version") == 0) {
            strncpy(out->version, value, APP_MANIFEST_VER_MAX - 1);
            out->version[APP_MANIFEST_VER_MAX - 1] = '\0';
        } else if (strcmp(key, "requires") == 0) {
            strncpy(out->requires, value, APP_MANIFEST_CAP_MAX - 1);
            out->requires[APP_MANIFEST_CAP_MAX - 1] = '\0';
        } else if (strcmp(key, "launcher") == 0) {
            out->show_in_launcher = (strcmp(value, "yes") == 0 ||
                                     strcmp(value, "1")   == 0 ||
                                     strcmp(value, "true") == 0);
        }
    }

    fclose(f);
    return true;
}

bool app_manifest_write(const char *app_name, const app_sd_manifest_t *manifest) {
    if (!app_name || !manifest) return false;

    char dir_path[192];
    snprintf(dir_path, sizeof(dir_path), "/sdcard/apps/%s", app_name);
    mkdir(dir_path, 0755);

    char path[256];
    snprintf(path, sizeof(path), "%s/manifest.cfg", dir_path);
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "name=%s\n", manifest->display_name);
    if (manifest->short_description[0])
        fprintf(f, "short_description=%s\n", manifest->short_description);
    if (manifest->long_description[0])
        fprintf(f, "long_description=%s\n", manifest->long_description);
    if (manifest->homepage[0])
        fprintf(f, "homepage=%s\n", manifest->homepage);
    if (manifest->version[0])
        fprintf(f, "version=%s\n", manifest->version);
    if (manifest->extensions[0])
        fprintf(f, "extensions=%s\n", manifest->extensions);
    if (manifest->requires[0])
        fprintf(f, "requires=%s\n", manifest->requires);
    fprintf(f, "launcher=%s\n", manifest->show_in_launcher ? "yes" : "no");

    fclose(f);
    return true;
}

char *app_manifest_get_display_name(const char *app_name, char *out, size_t out_size) {
    app_sd_manifest_t m;
    app_manifest_read(app_name, &m);
    strncpy(out, m.display_name, out_size - 1);
    out[out_size - 1] = '\0';
    return out;
}

int app_manifest_find_apps_for_ext(const char *ext, char (*app_names_out)[APP_MANIFEST_NAME_MAX], int max_apps) {
    if (!ext || !app_names_out || max_apps <= 0) return 0;

    // Strip leading dot if the caller included it
    if (ext[0] == '.') ext++;

    int count = 0;
    DIR *dir = opendir("/sdcard/apps");
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_apps) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        // Only consider directories that contain a program.elf
        char elf_path[192];
        snprintf(elf_path, sizeof(elf_path), "/sdcard/apps/%.160s/program.elf", entry->d_name);
        FILE *f = fopen(elf_path, "r");
        if (!f) continue;
        fclose(f);

        app_sd_manifest_t m;
        app_manifest_read(entry->d_name, &m);
        if (manifest_has_extension(&m, ext)) {
            snprintf(app_names_out[count], APP_MANIFEST_NAME_MAX, "%s", entry->d_name);
            count++;
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Found %d app(s) for extension '%s'", count, ext);
    return count;
}
