/*
 * Escaped-asterisk rendering unit test.
 *
 * Project Gutenberg headers use "\*\*\*" to render literal asterisks. The
 * tokenizer treats '\' as an escape prefix, so "\*" yields a literal '*'
 * inside a word token. This test guards against a regression where the
 * rendered line would erroneously begin with a leading space (previously
 * caused by the escape handling interacting with the renderer's leading-
 * whitespace logic).
 *
 * Build: see apps/reader/tests/run_tests.sh
 */
#include "greatest.h"
#include "reader_token.h"
#include "reader_types.h"
#include "reader_renderer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LINE_BUF_SIZE (53 + LINE_BUF_MARGIN)

static char *setup_lines(rendered_line_t *lines) {
    char *text_buf = malloc((size_t)MAX_RENDERED_LINES * LINE_BUF_SIZE);
    for (int i = 0; i < MAX_RENDERED_LINES; i++) {
        lines[i].text = text_buf + (size_t)i * LINE_BUF_SIZE;
        lines[i].text[0] = '\0';
        lines[i].color = 0;
        lines[i].attr = 0;
    }
    return text_buf;
}

TEST escaped_asterisks_do_not_cause_leading_space(void) {
    const char *input = "\\*\\*\\* START OF THE PROJECT GUTENBERG EBOOK A TALE OF TWO CITIES \\*\\*\\*\n";

    FILE *f = fmemopen((void *)input, strlen(input), "r");
    rendered_line_t lines[MAX_RENDERED_LINES];
    uint8_t heading_levels[MAX_RENDERED_LINES];
    memset(heading_levels, 0, sizeof(heading_levels));
    char *buf = setup_lines(lines);

    page_renderer_t r;
    renderer_init(&r, f, lines, heading_levels, 18, 53, LINE_BUF_SIZE);
    renderer_process_page(&r);

    ASSERTm("expected at least 1 rendered line", r.line_count >= 1);
    ASSERT_FALSEm("line must not start with a space", lines[0].text[0] == ' ');

    free(buf);
    fclose(f);
    PASS();
}

SUITE(star_escape_suite) {
    RUN_TEST(escaped_asterisks_do_not_cause_leading_space);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(star_escape_suite);
    GREATEST_MAIN_END();
}
