/*
 * Host-side unit tests for the config key-path validator
 * (main/app_config_validate.c).
 *
 * The validator is the security boundary of the config API: it prevents
 * directory traversal (../../etc) and path escapes that would let an app
 * write outside its /sdcard/apps/<app>/config/ sandbox.
 */
#include "greatest.h"
#include "app_config_validate.h"
#include <string.h>

/* ---- valid key paths ---- */

TEST simple_key(void) {
    ASSERT(appcfg_validate_key_path("settings"));
    PASS();
}

TEST nested_key(void) {
    ASSERT(appcfg_validate_key_path("ui/theme"));
    ASSERT(appcfg_validate_key_path("ui/theme/dark"));
    PASS();
}

TEST key_with_dots_in_name(void) {
    /* dots are allowed within segment names, just not '.' or '..' alone */
    ASSERT(appcfg_validate_key_path("file.txt"));
    ASSERT(appcfg_validate_key_path("config.json"));
    ASSERT(appcfg_validate_key_path("a.b.c"));
    PASS();
}

TEST key_with_underscores_dashes(void) {
    ASSERT(appcfg_validate_key_path("high_score"));
    ASSERT(appcfg_validate_key_path("high-score"));
    ASSERT(appcfg_validate_key_path("player_1-name"));
    PASS();
}

TEST single_char_key(void) {
    ASSERT(appcfg_validate_key_path("x"));
    PASS();
}

/* ---- traversal attacks (must be rejected) ---- */

TEST reject_dot_segment(void) {
    ASSERT_FALSE(appcfg_validate_key_path("./foo"));
    ASSERT_FALSE(appcfg_validate_key_path("foo/."));
    ASSERT_FALSE(appcfg_validate_key_path("foo/./bar"));
    PASS();
}

TEST reject_dotdot_segment(void) {
    ASSERT_FALSE(appcfg_validate_key_path("../foo"));
    ASSERT_FALSE(appcfg_validate_key_path("foo/.."));
    ASSERT_FALSE(appcfg_validate_key_path("foo/../bar"));
    PASS();
}

TEST reject_trailing_slash(void) {
    /* trailing '/' means the last segment is empty */
    ASSERT_FALSE(appcfg_validate_key_path("foo/"));
    PASS();
}

TEST reject_double_slash(void) {
    /* '//' means an empty segment */
    ASSERT_FALSE(appcfg_validate_key_path("foo//bar"));
    PASS();
}

TEST reject_leading_slash(void) {
    ASSERT_FALSE(appcfg_validate_key_path("/etc/passwd"));
    ASSERT_FALSE(appcfg_validate_key_path("/foo"));
    PASS();
}

TEST reject_empty_and_null(void) {
    ASSERT_FALSE(appcfg_validate_key_path(""));
    ASSERT_FALSE(appcfg_validate_key_path(NULL));
    PASS();
}

/* ---- interesting edge cases ---- */

TEST dots_after_alnum_are_ok(void) {
    /* 'a.' is NOT '.' alone — segment_dots gets poisoned to -100 by 'a' */
    ASSERT(appcfg_validate_key_path("a."));
    ASSERT(appcfg_validate_key_path("a.."));
    PASS();
}

TEST reject_invalid_chars(void) {
    ASSERT_FALSE(appcfg_validate_key_path("foo bar"));    /* space */
    ASSERT_FALSE(appcfg_validate_key_path("foo:bar"));    /* colon */
    ASSERT_FALSE(appcfg_validate_key_path("foo$bar"));    /* dollar */
    ASSERT_FALSE(appcfg_validate_key_path("foo\\bar"));   /* backslash */
    ASSERT_FALSE(appcfg_validate_key_path("foo|bar"));    /* pipe */
    PASS();
}

TEST deep_nesting_ok(void) {
    ASSERT(appcfg_validate_key_path("a/b/c/d/e/f/g/h"));
    PASS();
}

TEST just_dot_rejected(void) {
    ASSERT_FALSE(appcfg_validate_key_path("."));
    PASS();
}

TEST just_dotdot_rejected(void) {
    ASSERT_FALSE(appcfg_validate_key_path(".."));
    PASS();
}

/* ---- is_valid_segment_char ---- */

TEST segment_char_whitelist(void) {
    /* allowed */
    for (char c = 'a'; c <= 'z'; c++) ASSERT(appcfg_is_valid_segment_char(c));
    for (char c = 'A'; c <= 'Z'; c++) ASSERT(appcfg_is_valid_segment_char(c));
    for (char c = '0'; c <= '9'; c++) ASSERT(appcfg_is_valid_segment_char(c));
    ASSERT(appcfg_is_valid_segment_char('_'));
    ASSERT(appcfg_is_valid_segment_char('-'));
    ASSERT(appcfg_is_valid_segment_char('.'));

    /* rejected */
    ASSERT_FALSE(appcfg_is_valid_segment_char(' '));
    ASSERT_FALSE(appcfg_is_valid_segment_char('/'));
    ASSERT_FALSE(appcfg_is_valid_segment_char('\0'));
    ASSERT_FALSE(appcfg_is_valid_segment_char(':'));
    PASS();
}

/* ---- suites ---- */

SUITE(config_valid) {
    RUN_TEST(simple_key);
    RUN_TEST(nested_key);
    RUN_TEST(key_with_dots_in_name);
    RUN_TEST(key_with_underscores_dashes);
    RUN_TEST(single_char_key);
    RUN_TEST(dots_after_alnum_are_ok);
    RUN_TEST(deep_nesting_ok);
}

SUITE(config_traversal) {
    RUN_TEST(reject_dot_segment);
    RUN_TEST(reject_dotdot_segment);
    RUN_TEST(reject_trailing_slash);
    RUN_TEST(reject_double_slash);
    RUN_TEST(reject_leading_slash);
    RUN_TEST(reject_empty_and_null);
    RUN_TEST(reject_invalid_chars);
    RUN_TEST(just_dot_rejected);
    RUN_TEST(just_dotdot_rejected);
}

SUITE(config_charset) {
    RUN_TEST(segment_char_whitelist);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(config_valid);
    RUN_SUITE(config_traversal);
    RUN_SUITE(config_charset);
    GREATEST_MAIN_END();
}
