#ifndef SEMVER_H
#define SEMVER_H

/*
 * Minimal semantic-version helpers used by the OTA update path to decide
 * whether an offered release is newer than the running firmware.
 *
 * Extracted from main/ota_update.c into its own translation unit so it can
 * be unit-tested on the host without pulling in ESP-IDF headers. The
 * implementation depends only on the C standard library.
 */

/*
 * Parse a leading "v1.2.3" (optional 'v'/'V' prefix) from s.
 * *major, *minor, *patch receive the parsed numbers (0 on parse failure).
 * Returns a pointer to the first character after the patch field —
 * typically '-' for a pre-release suffix, '+' for build metadata, or '\0'
 * for end of string. Returns NULL if s is NULL.
 */
const char *parse_semver(const char *s, int *major, int *minor, int *patch);

/*
 * Compare two version strings per semver precedence rules.
 * Returns 1 if a > b, -1 if a < b, 0 if equal.
 * A version with a pre-release suffix is LOWER than the same version without
 * one (per the semver spec). Build metadata (after '+') is ignored.
 *
 * Pre-release precedence follows semver.org: identifiers are split on '.',
 * numeric identifiers are compared numerically (and rank below non-numeric
 * ones at the same position), non-numeric identifiers are compared lexically,
 * and a larger set of fields outranks a smaller one when all preceding fields
 * are equal. Leading zeros in numeric identifiers are tolerated.
 */
int compare_versions(const char *a, const char *b);

#endif /* SEMVER_H */
