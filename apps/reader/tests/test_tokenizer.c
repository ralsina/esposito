/*
 * Tokenizer unit tests — core token classification.
 *
 * Verifies that the markdown tokenizer (apps/reader/reader_token.c) emits the
 * correct token type and text for words, spaces, newlines, heading markers
 * (#), quote markers (>), and EOF. HTML tag stripping is covered separately
 * by test_html_strip.c.
 *
 * Build: see apps/reader/tests/run_tests.sh
 */
#include "greatest.h"
#include "reader_token.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *input;
    const char *desc;
    struct { token_type_t type; const char *text; } expect[16];
    int n_expect;
} tok_case;

static const tok_case cases[] = {
    {
        "# Title\n",
        "heading marker + word",
        {
            { TOKEN_HASH, "#" },
            { TOKEN_SPACE, " " },
            { TOKEN_WORD, "Title" },
            { TOKEN_NEWLINE, "\n" },
        },
        4,
    },
    {
        "> quote\n",
        "quote marker + word",
        {
            { TOKEN_GT, ">" },
            { TOKEN_SPACE, " " },
            { TOKEN_WORD, "quote" },
            { TOKEN_NEWLINE, "\n" },
        },
        4,
    },
    {
        "one two three\n",
        "multiple words separated by single spaces",
        {
            { TOKEN_WORD, "one" },
            { TOKEN_SPACE, " " },
            { TOKEN_WORD, "two" },
            { TOKEN_SPACE, " " },
            { TOKEN_WORD, "three" },
            { TOKEN_NEWLINE, "\n" },
        },
        6,
    },
    {
        "a\n\nb\n",
        "blank line produces two consecutive newlines",
        {
            { TOKEN_WORD, "a" },
            { TOKEN_NEWLINE, "\n" },
            { TOKEN_NEWLINE, "\n" },
            { TOKEN_WORD, "b" },
            { TOKEN_NEWLINE, "\n" },
        },
        5,
    },
};

TEST tokenizer_emits_expected_sequence(void) {
    char report[2048];
    int fails = 0;
    size_t n = sizeof(cases) / sizeof(cases[0]);

    for (size_t i = 0; i < n; i++) {
        FILE *f = fmemopen((void *)cases[i].input, strlen(cases[i].input), "r");
        ASSERT(f != NULL);

        tokenizer_t tok;
        tokenizer_init(&tok, f);

        int idx = 0;
        while (tokenizer_next(&tok)) {
            token_t *t = tokenizer_current(&tok);
            if (!t || t->type == TOKEN_EOF) break;
            if (idx >= cases[i].n_expect) {
                int off = (int)strlen(report);
                snprintf(report + off, sizeof(report) - off,
                         "\n  [%zu] '%s': too many tokens (got >=%d, expected %d)",
                         i, cases[i].desc, idx + 1, cases[i].n_expect);
                fails++;
                break;
            }
            if (t->type != cases[i].expect[idx].type ||
                strcmp(t->text, cases[i].expect[idx].text) != 0) {
                int off = (int)strlen(report);
                snprintf(report + off, sizeof(report) - off,
                         "\n  [%zu] '%s' token %d: got type=%d text='%s', "
                         "expected type=%d text='%s'",
                         i, cases[i].desc, idx,
                         t->type, t->text,
                         cases[i].expect[idx].type, cases[i].expect[idx].text);
                fails++;
            }
            idx++;
        }
        if (idx != cases[i].n_expect && fails == 0) {
            int off = (int)strlen(report);
            snprintf(report + off, sizeof(report) - off,
                     "\n  [%zu] '%s': too few tokens (got %d, expected %d)",
                     i, cases[i].desc, idx, cases[i].n_expect);
            fails++;
        }
        fclose(f);
    }
    if (fails) FAILm(report);
    PASS();
}

TEST tokenizer_eof_on_empty_input(void) {
    FILE *f = fmemopen((void *)"", 0, "r");
    ASSERT(f != NULL);
    tokenizer_t tok;
    tokenizer_init(&tok, f);
    ASSERT_FALSE(tokenizer_next(&tok));
    token_t *t = tokenizer_current(&tok);
    ASSERT(t != NULL);
    ASSERT_EQ(TOKEN_EOF, t->type);
    fclose(f);
    PASS();
}

SUITE(tokenizer_suite) {
    RUN_TEST(tokenizer_emits_expected_sequence);
    RUN_TEST(tokenizer_eof_on_empty_input);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(tokenizer_suite);
    GREATEST_MAIN_END();
}
