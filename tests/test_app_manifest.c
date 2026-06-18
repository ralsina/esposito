/*
 * Host-side unit tests for the manifest matching/parsing logic
 * (main/app_manifest_match.c).
 *
 * Tests extension matching (case-insensitive, comma-separated) and the
 * per-line manifest.cfg parser (key=value extraction).
 */
#include "greatest.h"
#include "app_manifest_match.h"
#include "app_manifest.h"
#include <string.h>

/* ---- manifest_has_extension ---- */

TEST ext_single_match(void) {
    app_sd_manifest_t m = {0};
    strncpy(m.extensions, "txt", sizeof(m.extensions) - 1);
    ASSERT(manifest_has_extension(&m, "txt"));
    ASSERT_FALSE(manifest_has_extension(&m, "md"));
    PASS();
}

TEST ext_multiple_match(void) {
    app_sd_manifest_t m = {0};
    strncpy(m.extensions, "txt,md,c", sizeof(m.extensions) - 1);
    ASSERT(manifest_has_extension(&m, "txt"));
    ASSERT(manifest_has_extension(&m, "md"));
    ASSERT(manifest_has_extension(&m, "c"));
    ASSERT_FALSE(manifest_has_extension(&m, "cpp"));
    PASS();
}

TEST ext_case_insensitive(void) {
    app_sd_manifest_t m = {0};
    strncpy(m.extensions, "JPG,jpeg", sizeof(m.extensions) - 1);
    ASSERT(manifest_has_extension(&m, "jpg"));
    ASSERT(manifest_has_extension(&m, "JPG"));
    ASSERT(manifest_has_extension(&m, "jpeg"));
    ASSERT(manifest_has_extension(&m, "JPEG"));
    ASSERT_FALSE(manifest_has_extension(&m, "png"));
    PASS();
}

TEST ext_empty_string(void) {
    app_sd_manifest_t m = {0};
    m.extensions[0] = '\0';
    ASSERT_FALSE(manifest_has_extension(&m, "txt"));
    PASS();
}

TEST ext_prefix_no_false_positive(void) {
    /* "tx" should NOT match "txt" — length must be equal */
    app_sd_manifest_t m = {0};
    strncpy(m.extensions, "txt", sizeof(m.extensions) - 1);
    ASSERT_FALSE(manifest_has_extension(&m, "tx"));
    ASSERT_FALSE(manifest_has_extension(&m, "txtx"));
    PASS();
}

TEST ext_single_char_ext(void) {
    app_sd_manifest_t m = {0};
    strncpy(m.extensions, "c,h", sizeof(m.extensions) - 1);
    ASSERT(manifest_has_extension(&m, "c"));
    ASSERT(manifest_has_extension(&m, "h"));
    PASS();
}

TEST ext_trailing_comma(void) {
    /* trailing comma produces an empty token at the end — should not match
     * anything spuriously, and should not crash */
    app_sd_manifest_t m = {0};
    strncpy(m.extensions, "txt,", sizeof(m.extensions) - 1);
    ASSERT(manifest_has_extension(&m, "txt"));
    ASSERT_FALSE(manifest_has_extension(&m, ""));
    PASS();
}

/* ---- manifest_parse_line ---- */

static app_sd_manifest_t make_default(void) {
    app_sd_manifest_t m = {0};
    m.show_in_launcher = true;
    return m;
}

TEST parse_name(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "name=My App";
    manifest_parse_line(&m, line);
    ASSERT_STR_EQ("My App", m.display_name);
    PASS();
}

TEST parse_extensions(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "extensions=jpg,jpeg,png";
    manifest_parse_line(&m, line);
    ASSERT_STR_EQ("jpg,jpeg,png", m.extensions);
    PASS();
}

TEST parse_launcher_yes(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "launcher=yes";
    manifest_parse_line(&m, line);
    ASSERT(m.show_in_launcher);
    PASS();
}

TEST parse_launcher_no(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "launcher=no";
    manifest_parse_line(&m, line);
    ASSERT_FALSE(m.show_in_launcher);
    PASS();
}

TEST parse_launcher_true(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "launcher=true";
    manifest_parse_line(&m, line);
    ASSERT(m.show_in_launcher);
    PASS();
}

TEST parse_strips_newline(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "name=Test\n";
    manifest_parse_line(&m, line);
    ASSERT_STR_EQ("Test", m.display_name);
    PASS();
}

TEST parse_strips_crlf(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "name=Test\r\n";
    manifest_parse_line(&m, line);
    ASSERT_STR_EQ("Test", m.display_name);
    PASS();
}

TEST parse_line_without_equals(void) {
    /* lines without '=' are ignored, no crash */
    app_sd_manifest_t m = make_default();
    char line[] = "just a comment";
    manifest_parse_line(&m, line);
    /* nothing should change */
    ASSERT_STR_EQ("", m.display_name);
    PASS();
}

TEST parse_unknown_key_ignored(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "unknown_key=value";
    manifest_parse_line(&m, line);
    /* no crash, no change */
    ASSERT_STR_EQ("", m.extensions);
    PASS();
}

TEST parse_empty_value(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "name=";
    manifest_parse_line(&m, line);
    ASSERT_STR_EQ("", m.display_name);
    PASS();
}

TEST parse_description_alias(void) {
    /* "description" is an alias for "short_description" */
    app_sd_manifest_t m = make_default();
    char line[] = "description=A cool app";
    manifest_parse_line(&m, line);
    ASSERT_STR_EQ("A cool app", m.short_description);
    PASS();
}

TEST parse_version(void) {
    app_sd_manifest_t m = make_default();
    char line[] = "version=1.2.3";
    manifest_parse_line(&m, line);
    ASSERT_STR_EQ("1.2.3", m.version);
    PASS();
}

TEST parse_multiple_lines(void) {
    app_sd_manifest_t m = make_default();
    char l1[] = "name=Reader";
    char l2[] = "extensions=txt,md";
    char l3[] = "launcher=no";
    char l4[] = "version=2.0";
    manifest_parse_line(&m, l1);
    manifest_parse_line(&m, l2);
    manifest_parse_line(&m, l3);
    manifest_parse_line(&m, l4);

    ASSERT_STR_EQ("Reader", m.display_name);
    ASSERT_STR_EQ("txt,md", m.extensions);
    ASSERT_FALSE(m.show_in_launcher);
    ASSERT_STR_EQ("2.0", m.version);
    PASS();
}

/* ---- suites ---- */

SUITE(manifest_ext) {
    RUN_TEST(ext_single_match);
    RUN_TEST(ext_multiple_match);
    RUN_TEST(ext_case_insensitive);
    RUN_TEST(ext_empty_string);
    RUN_TEST(ext_prefix_no_false_positive);
    RUN_TEST(ext_single_char_ext);
    RUN_TEST(ext_trailing_comma);
}

SUITE(manifest_parse) {
    RUN_TEST(parse_name);
    RUN_TEST(parse_extensions);
    RUN_TEST(parse_launcher_yes);
    RUN_TEST(parse_launcher_no);
    RUN_TEST(parse_launcher_true);
    RUN_TEST(parse_strips_newline);
    RUN_TEST(parse_strips_crlf);
    RUN_TEST(parse_line_without_equals);
    RUN_TEST(parse_unknown_key_ignored);
    RUN_TEST(parse_empty_value);
    RUN_TEST(parse_description_alias);
    RUN_TEST(parse_version);
    RUN_TEST(parse_multiple_lines);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(manifest_ext);
    RUN_SUITE(manifest_parse);
    GREATEST_MAIN_END();
}
