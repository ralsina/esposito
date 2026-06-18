#include "semver.h"
#include <stdlib.h>   /* atoi */
#include <string.h>   /* memcmp */
#include <stdbool.h>  /* bool */

const char *parse_semver(const char *s, int *major, int *minor, int *patch) {
    *major = *minor = *patch = 0;
    if (!s) return NULL;
    if (*s == 'v' || *s == 'V') s++;

    *major = atoi(s);
    while (*s && *s != '.' && *s != '-' && *s != '+') s++;
    if (*s == '.') s++;

    *minor = atoi(s);
    while (*s && *s != '.' && *s != '-' && *s != '+') s++;
    if (*s == '.') s++;

    *patch = atoi(s);
    while (*s && *s != '-' && *s != '+') s++;
    return s;
}

/* Is the identifier [p, p+len) purely numeric (all ASCII digits)? */
static bool id_is_numeric(const char *p, size_t len) {
    if (len == 0) return false;
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
    }
    return true;
}

/* Compare two numeric identifiers as unsigned integers, tolerating leading
 * zeros (invalid per spec, but parsed tolerantly). Returns -1/0/1. */
static int compare_numeric_id(const char *a, size_t a_len,
                              const char *b, size_t b_len) {
    while (a_len > 1 && *a == '0') { a++; a_len--; }
    while (b_len > 1 && *b == '0') { b++; b_len--; }
    if (a_len != b_len) return a_len < b_len ? -1 : 1;
    int r = memcmp(a, b, a_len);
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

/*
 * Compare two pre-release strings — each the text after the leading '-',
 * a dot-separated list of identifiers — per semver precedence:
 *   - identifiers are compared left to right;
 *   - numeric identifiers are compared numerically, and always have lower
 *     precedence than non-numeric identifiers at the same position;
 *   - non-numeric identifiers are compared lexically (ASCII byte order);
 *   - a larger set of fields has higher precedence when all preceding
 *     identifiers are equal.
 * Both a and b are non-NULL here (the release-vs-prerelease case is handled
 * by the caller). Returns -1/0/1.
 */
static int compare_prerelease(const char *a, const char *b) {
    while (*a && *b) {
        const char *a_id = a;
        const char *b_id = b;
        while (*a && *a != '.') a++;
        while (*b && *b != '.') b++;
        size_t a_len = (size_t)(a - a_id);
        size_t b_len = (size_t)(b - b_id);

        bool a_num = id_is_numeric(a_id, a_len);
        bool b_num = id_is_numeric(b_id, b_len);
        if (a_num != b_num) {
            return a_num ? -1 : 1;  /* numeric < non-numeric */
        }

        int r;
        if (a_num) {
            r = compare_numeric_id(a_id, a_len, b_id, b_len);
        } else {
            size_t min_len = a_len < b_len ? a_len : b_len;
            int c = min_len ? memcmp(a_id, b_id, min_len) : 0;
            if (c < 0) r = -1;
            else if (c > 0) r = 1;
            else r = (a_len == b_len) ? 0 : (a_len < b_len ? -1 : 1);
        }
        if (r != 0) return r;

        if (*a == '.') a++;
        if (*b == '.') b++;
    }

    /* One or both ran out of identifiers. A larger set of fields has higher
     * precedence; equal sets are equal. */
    if (*a == '\0' && *b == '\0') return 0;
    return *a ? 1 : -1;
}

int compare_versions(const char *a, const char *b) {
    int amaj, amin, apat, bmaj, bmin, bpat;
    const char *a_rest = parse_semver(a, &amaj, &amin, &apat);
    const char *b_rest = parse_semver(b, &bmaj, &bmin, &bpat);

    if (amaj != bmaj) return amaj < bmaj ? -1 : 1;
    if (amin != bmin) return amin < bmin ? -1 : 1;
    if (apat != bpat) return apat < bpat ? -1 : 1;

    /* Equal X.Y.Z. Pre-release has lower precedence than no pre-release. */
    bool a_pre = a_rest && *a_rest == '-';
    bool b_pre = b_rest && *b_rest == '-';
    if (a_pre && !b_pre) return -1;
    if (!a_pre && b_pre) return 1;
    if (a_pre && b_pre) {
        return compare_prerelease(a_rest + 1, b_rest + 1);
    }
    return 0;
}
