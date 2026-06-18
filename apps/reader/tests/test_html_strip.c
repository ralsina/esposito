/*
 * HTML tag stripping unit tests.
 *
 * Verifies that the tokenizer (apps/reader/reader_token.c) discards HTML tags
 * while preserving surrounding text. Tag stripping happens inside
 * read_raw_byte: a '<' consumes everything up to and including the next '>'.
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
    struct { token_type_t type; const char *text; } expect[8];
    int n_expect;
} strip_case;

static const strip_case cases[] = {
    {
        "Hello <b>world</b> foo\n",
        "simple open+close tags",
        {
            { TOKEN_WORD, "Hello" },
            { TOKEN_SPACE, " " },
            { TOKEN_WORD, "world" },
            { TOKEN_SPACE, " " },
            { TOKEN_WORD, "foo" },
            { TOKEN_NEWLINE, "\n" },
        },
        6,
    },
    {
        "Before <span id=\"foo\" class=\"bar\">after\n",
        "tag with attributes",
        {
            { TOKEN_WORD, "Before" },
            { TOKEN_SPACE, " " },
            { TOKEN_WORD, "after" },
            { TOKEN_NEWLINE, "\n" },
        },
        4,
    },
    {
        "<td><a href=\"#link\">CHAPTER</a></td>\n",
        "nested consecutive tags",
        {
            { TOKEN_WORD, "CHAPTER" },
            { TOKEN_NEWLINE, "\n" },
        },
        2,
    },
    {
        "Cities</span>\nNext\n",
        "closing tag at end of line",
        {
            { TOKEN_WORD, "Cities" },
            { TOKEN_NEWLINE, "\n" },
            { TOKEN_WORD, "Next" },
            { TOKEN_NEWLINE, "\n" },
        },
        4,
    },
};

TEST html_tags_are_stripped(void) {
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

SUITE(html_strip_suite) {
    RUN_TEST(html_tags_are_stripped);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(html_strip_suite);
    GREATEST_MAIN_END();
}
