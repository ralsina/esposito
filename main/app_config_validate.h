#ifndef APP_CONFIG_VALIDATE_H
#define APP_CONFIG_VALIDATE_H

/*
 * App config key-path validation — the security boundary of the config API.
 *
 * Apps persist settings via config_get/set(key_path, ...) which resolves to
 * /sdcard/apps/<app>/config/<key_path>. The key_path validator prevents
 * directory traversal (../../etc/passwd) and path escapes that would let a
 * buggy or malicious app write outside its sandbox.
 *
 * Pure logic — no ESP-IDF, no I/O. Host-compilable and unit-tested by
 * tests/test_app_config.c.
 */
#include <stdbool.h>

/* Check if a character is valid in a config key-path segment.
 * Allowed: alphanumeric, '_', '-', '.'. */
bool appcfg_is_valid_segment_char(char ch);

/* Validate a config key_path. Returns true if safe (stays within the app's
 * config sandbox), false if it could escape via traversal.
 *
 * Rules:
 * - Non-null, non-empty, must not start with '/'
 * - Each '/'-separated segment must be non-empty
 * - Segments '.' and '..' are rejected (traversal)
 * - Only is_valid_segment_char characters allowed
 */
bool appcfg_validate_key_path(const char *key_path);

#endif /* APP_CONFIG_VALIDATE_H */
