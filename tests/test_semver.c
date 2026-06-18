/*
 * Host-side unit tests for the semver helpers (main/semver.c).
 *
 * Build/run: see tests/run_tests.sh
 *   gcc tests/test_semver.c main/semver.c -Itests -Imain -o /tmp/test_semver
 *
 * Framework: greatest (tests/greatest.h), ISC-style licensed, vendored.
 *
 * Design: cases assert the *current* behavior of the implementation, so the
 * suite acts as a regression net. The one known spec deviation (multi-digit
 * numeric pre-release identifiers compared via strcmp) is pinned in
 * compare_versions_table and also flagged by a SKIPPED test.
 */
#include "greatest.h"
#include "semver.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* compare_versions: table-driven precedence                          */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *a;
    const char *b;
    int expected;   /* -1, 0, 1 */
    const char *desc;
} cmp_case;

static const cmp_case cmp_cases[] = {
    /* equality */
    { "v1.0.0",      "v1.0.0",      0, "equal stable" },
    { "1.0.0",       "1.0.0",       0, "equal, no v prefix" },
    { "v1.2.3",      "1.2.3",       0, "v prefix ignored" },
    { "V1.2.3",      "1.2.3",       0, "uppercase V prefix ignored" },

    /* major / minor / patch ordering */
    { "1.0.0",       "2.0.0",      -1, "major less" },
    { "2.0.0",       "1.0.0",       1, "major greater" },
    { "1.2.0",       "1.3.0",      -1, "minor less" },
    { "1.0.1",       "1.0.2",      -1, "patch less" },
    { "1.0.2",       "1.0.1",       1, "patch greater" },

    /* pre-release vs release (spec: pre-release is lower) */
    { "1.0.0-rc.1",  "1.0.0",      -1, "prerelease < release" },
    { "1.0.0",       "1.0.0-rc.1",  1, "release > prerelease" },

    /* pre-release vs pre-release */
    { "1.0.0-alpha", "1.0.0-beta", -1, "alpha < beta (lexical)" },
    { "1.0.0-beta",  "1.0.0-alpha", 1, "beta > alpha (lexical)" },
    { "1.0.0-rc.1",  "1.0.0-rc.2", -1, "rc.1 < rc.2 (single digit)" },

    /* numeric pre-release identifiers compared numerically (spec-correct) */
    { "1.0.0-2",     "1.0.0-10",   -1, "numeric: 2 < 10" },
    { "1.0.0-10",    "1.0.0-2",     1, "numeric: 10 > 2" },
    { "1.0.0-beta.2",  "1.0.0-beta.10", -1, "beta.2 < beta.10 (OTA beta channel)" },
    { "1.0.0-beta.10", "1.0.0-beta.2",   1, "beta.10 > beta.2" },

    /* numeric identifiers rank below non-numeric at the same position */
    { "1.0.0-1",     "1.0.0-alpha", -1, "numeric < non-numeric" },
    { "1.0.0-alpha.1", "1.0.0-alpha.beta", -1, "2nd id: 1 < beta (num<non-num)" },

    /* more fields outrank fewer when preceding match */
    { "1.0.0-alpha", "1.0.0-alpha.1", -1, "fewer fields < more fields" },
    { "1.0.0-alpha.1", "1.0.0-alpha",  1, "more fields > fewer fields" },

    /* leading zeros tolerated in numeric identifiers */
    { "1.0.0-01",    "1.0.0-1",      0, "leading zero ignored" },

    /* mixed: second identifier decides after first matches */
    { "1.0.0-1.alpha", "1.0.0-1.beta.1", -1, "2nd id alpha < beta" },

    /* build metadata is ignored */
    { "1.0.0+build1", "1.0.0+build2", 0, "build metadata ignored" },
    { "1.0.0+build1", "1.0.0",        0, "build metadata vs none" },

    /* tolerant parsing */
    { "1.2",         "1.2.0",       0, "missing patch defaults to 0" },
    { "1.0",         "1.0.0.0",     0, "extra dot-components ignored" },
};

TEST compare_versions_table(void) {
    char report[2048];
    report[0] = '\0';
    int fails = 0;
    size_t n = sizeof(cmp_cases) / sizeof(cmp_cases[0]);

    for (size_t i = 0; i < n; i++) {
        int got = compare_versions(cmp_cases[i].a, cmp_cases[i].b);
        if (got != cmp_cases[i].expected) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] \"%s\" vs \"%s\": expected %d, got %d  (%s)",
                     i, cmp_cases[i].a, cmp_cases[i].b,
                     cmp_cases[i].expected, got, cmp_cases[i].desc);
            fails++;
        }
    }
    if (fails) { FAILm(report); }
    PASS();
}

/* compare_versions must be antisymmetric: cmp(a,b) == -cmp(b,a).
 * Cross-checks every table pair without re-stating expected values. */
TEST compare_versions_antisymmetry(void) {
    char report[1024];
    report[0] = '\0';
    int fails = 0;
    size_t n = sizeof(cmp_cases) / sizeof(cmp_cases[0]);

    for (size_t i = 0; i < n; i++) {
        int fwd = compare_versions(cmp_cases[i].a, cmp_cases[i].b);
        int rev = compare_versions(cmp_cases[i].b, cmp_cases[i].a);
        if (fwd != -rev) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] \"%s\"/\"%s\": fwd=%d rev=%d (not antisymmetric)",
                     i, cmp_cases[i].a, cmp_cases[i].b, fwd, rev);
            fails++;
        }
    }
    if (fails) { FAILm(report); }
    PASS();
}

/* ------------------------------------------------------------------ */
/* parse_semver: parser outputs                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *in;
    int maj, min, pat;
    const char *rest;   /* expected string at the returned pointer */
} parse_case;

static const parse_case parse_cases[] = {
    { "v1.2.3",       1, 2, 3, ""        },
    { "1.2.3",        1, 2, 3, ""        },
    { "V1.2.3",       1, 2, 3, ""        },
    { "v1.2.3-rc.1",  1, 2, 3, "-rc.1"  },
    { "1.0.0+build",  1, 0, 0, "+build" },
    { "v1.2",         1, 2, 0, ""        },
    { "1",            1, 0, 0, ""        },
    { "v1.0.0-10",    1, 0, 0, "-10"    },
    { "1.2.3.4",      1, 2, 3, ""        }, /* 4th component silently consumed by patch scan */
};

TEST parse_semver_table(void) {
    char report[1024];
    report[0] = '\0';
    int fails = 0;
    size_t n = sizeof(parse_cases) / sizeof(parse_cases[0]);

    for (size_t i = 0; i < n; i++) {
        int m, mi, p;
        const char *rest = parse_semver(parse_cases[i].in, &m, &mi, &p);
        const char *exp_rest = parse_cases[i].rest ? parse_cases[i].rest : "";
        if (m != parse_cases[i].maj || mi != parse_cases[i].min ||
            p != parse_cases[i].pat ||
            rest == NULL || strcmp(rest, exp_rest) != 0) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] \"%s\": got %d.%d.%d rest=\"%s\", "
                     "expected %d.%d.%d rest=\"%s\"",
                     i, parse_cases[i].in,
                     m, mi, p, rest ? rest : "(null)",
                     parse_cases[i].maj, parse_cases[i].min, parse_cases[i].pat,
                     exp_rest);
            fails++;
        }
    }
    if (fails) { FAILm(report); }
    PASS();
}

/* NULL input must not crash and must zero the outputs. */
TEST parse_semver_null_safe(void) {
    int m = 7, mi = 7, p = 7;
    const char *rest = parse_semver(NULL, &m, &mi, &p);
    ASSERT(rest == NULL);
    ASSERT_EQ(0, m);
    ASSERT_EQ(0, mi);
    ASSERT_EQ(0, p);
    PASS();
}

/* Regression guard for the pre-release numeric-comparison fix: multi-digit
 * numeric identifiers must be compared numerically, not lexically. Before the
 * fix, strcmp treated "1.0.0-beta.10" as OLDER than "1.0.0-beta.2", which
 * caused the OTA beta channel to refuse legitimate upgrades. */
TEST semver_prerelease_numeric_correct(void) {
    ASSERT_EQ(-1, compare_versions("v1.0.0-beta.2",  "v1.0.0-beta.10"));
    ASSERT_EQ(1,  compare_versions("v1.0.0-beta.10", "v1.0.0-beta.2"));
    ASSERT_EQ(-1, compare_versions("1.0.0-2",  "1.0.0-10"));
    ASSERT_EQ(1,  compare_versions("1.0.0-10", "1.0.0-2"));
    ASSERT_EQ(0,  compare_versions("1.0.0-01", "1.0.0-1"));
    /* numeric ranks below non-numeric at the same position */
    ASSERT_EQ(-1, compare_versions("1.0.0-1", "1.0.0-alpha"));
    PASS();
}

SUITE(semver_suite) {
    RUN_TEST(compare_versions_table);
    RUN_TEST(compare_versions_antisymmetry);
    RUN_TEST(parse_semver_table);
    RUN_TEST(parse_semver_null_safe);
    RUN_TEST(semver_prerelease_numeric_correct);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();      /* parses CLI flags (-v, --list, ...) */
    RUN_SUITE(semver_suite);
    GREATEST_MAIN_END();        /* exit code: 0 pass, non-zero fail */
}
