/*
 * Quote block rendering unit tests.
 *
 * Verifies that the page renderer (apps/reader/reader_renderer.c) indents
 * quote blocks, preserves their text, separates them from following
 * paragraphs, and gives them a distinct color.
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

/* Allocate a contiguous text buffer for MAX_RENDERED_LINES rendered_line_t
 * entries. Returns the buffer (free with free()); fills `lines`. */
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

static void render_input(const char *input, rendered_line_t *lines,
                         uint8_t *heading_levels, page_renderer_t *renderer) {
    FILE *f = fmemopen((void *)input, strlen(input), "r");
    memset(heading_levels, 0, sizeof(uint8_t) * MAX_RENDERED_LINES);
    renderer_init(renderer, f, lines, heading_levels, 18, 53, LINE_BUF_SIZE);
    renderer_process_page(renderer);
    fclose(f);
}

TEST simple_quote_block_is_indented(void) {
    rendered_line_t lines[MAX_RENDERED_LINES];
    uint8_t heading_levels[MAX_RENDERED_LINES];
    char *buf = setup_lines(lines);
    page_renderer_t r;
    render_input("> This is a quote\n\nNormal text\n", lines, heading_levels, &r);

    ASSERTm("expected at least 3 lines", r.line_count >= 3);
    ASSERT_EQm("quote line should start with a space", ' ', lines[0].text[0]);
    ASSERT_EQm("quote line second char should be space", ' ', lines[0].text[1]);
    ASSERTm("quote line should contain 'This is a quote'",
            strstr(lines[0].text, "This is a quote") != NULL);
    ASSERTm("line 2 should contain 'Normal text'",
            strstr(lines[2].text, "Normal text") != NULL);

    free(buf);
    PASS();
}

TEST multiline_quote_block(void) {
    rendered_line_t lines[MAX_RENDERED_LINES];
    uint8_t heading_levels[MAX_RENDERED_LINES];
    char *buf = setup_lines(lines);
    page_renderer_t r;
    render_input("> Line one\n> Line two\n\nAfter quote\n",
                 lines, heading_levels, &r);

    ASSERTm("expected at least 4 lines", r.line_count >= 4);
    ASSERTm("line 0 should contain 'Line one'",
            strstr(lines[0].text, "Line one") != NULL);
    ASSERTm("line 1 should contain 'Line two'",
            strstr(lines[1].text, "Line two") != NULL);
    ASSERTm("line 3 should contain 'After quote'",
            strstr(lines[3].text, "After quote") != NULL);

    free(buf);
    PASS();
}

TEST quote_block_has_distinct_color(void) {
    rendered_line_t lines[MAX_RENDERED_LINES];
    uint8_t heading_levels[MAX_RENDERED_LINES];
    char *buf = setup_lines(lines);
    page_renderer_t r;
    render_input("> Quoted text\n\nNormal text\n", lines, heading_levels, &r);

    ASSERTm("expected at least 2 lines", r.line_count >= 2);
    ASSERTm("quote and normal lines must differ in color",
            lines[0].color != lines[1].color);

    free(buf);
    PASS();
}

SUITE(quote_block_suite) {
    RUN_TEST(simple_quote_block_is_indented);
    RUN_TEST(multiline_quote_block);
    RUN_TEST(quote_block_has_distinct_color);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(quote_block_suite);
    GREATEST_MAIN_END();
}
