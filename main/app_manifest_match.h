#ifndef APP_MANIFEST_MATCH_H
#define APP_MANIFEST_MATCH_H

/*
 * App manifest matching and parsing — pure logic extracted from
 * app_manifest.c for host-side testing.
 *
 * No ESP-IDF, no file I/O. Host-compilable and unit-tested by
 * tests/test_app_manifest.c.
 */
#include <stdbool.h>
#include <stddef.h>
#include "app_manifest.h"   /* app_sd_manifest_t, APP_MANIFEST_*_MAX */

/* Case-insensitive comparison of the first n chars of a and b.
 * Returns 1 if equal, 0 if different. */
int manifest_str_tolower_eq_n(const char *a, const char *b, size_t n);

/* Check whether ext (no leading dot, e.g. "jpg") is listed in manifest's
 * comma-separated extensions string (e.g. "jpg,jpeg"). Case-insensitive. */
bool manifest_has_extension(const app_sd_manifest_t *m, const char *ext);

/* Parse a single "key=value" manifest line into the manifest struct.
 * The line buffer is modified in place (split on '='). Lines without '=' are
 * ignored (return true but do nothing). Trailing \n and \r are stripped.
 *
 * This is the per-line parsing logic extracted from app_manifest_read(),
 * making it testable without file I/O. */
bool manifest_parse_line(app_sd_manifest_t *out, char *line);

#endif /* APP_MANIFEST_MATCH_H */
