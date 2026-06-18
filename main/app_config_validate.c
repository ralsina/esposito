/*
 * Pure config key-path validation logic — no ESP-IDF, no I/O, no allocation.
 * See app_config_validate.h for the contract. Tested by tests/test_app_config.c.
 */
#include "app_config_validate.h"
#include <ctype.h>

bool appcfg_is_valid_segment_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.';
}

bool appcfg_validate_key_path(const char *key_path) {
    if (!key_path || !key_path[0]) {
        return false;
    }
    if (key_path[0] == '/') {
        return false;
    }

    bool in_segment = false;
    int segment_dots = 0;

    for (const char *cursor = key_path;; cursor++) {
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

        if (!appcfg_is_valid_segment_char(ch)) {
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
