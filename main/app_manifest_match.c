/*
 * Pure manifest matching/parsing logic — no ESP-IDF, no I/O.
 * See app_manifest_match.h for the contract. Tested by tests/test_app_manifest.c.
 */
#include "app_manifest_match.h"
#include <string.h>
#include <ctype.h>

int manifest_str_tolower_eq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

bool manifest_has_extension(const app_sd_manifest_t *m, const char *ext) {
    if (!m->extensions[0]) return false;
    size_t ext_len = strlen(ext);
    const char *p = m->extensions;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t token_len = comma ? (size_t)(comma - p) : strlen(p);
        if (ext_len == token_len && manifest_str_tolower_eq_n(ext, p, token_len)) {
            return true;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return false;
}

bool manifest_parse_line(app_sd_manifest_t *out, char *line) {
    if (!out || !line) return false;

    /* Strip trailing newline / carriage return */
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    char *cr = strchr(line, '\r');
    if (cr) *cr = '\0';

    char *eq = strchr(line, '=');
    if (!eq) return true;  /* not a key=value line; not an error */
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

    return true;
}
