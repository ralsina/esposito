#include "reader_token.h"
#include "reader_types.h"
#include "reader_renderer.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

int main(void) {
    int failed = 0;

    // Test 1: Simple tag stripping
    printf("Test 1: Simple tag stripping\n");
    {
        const char *input = "Hello <b>world</b> foo\n";
        FILE *f = fmemopen((void *)input, strlen(input), "r");
        tokenizer_t tok;
        tokenizer_init(&tok, f);

        const char *expected[] = {"Hello", " ", "world", " ", "foo", "\n"};
        int expected_types[] = {TOKEN_WORD, TOKEN_SPACE, TOKEN_WORD, TOKEN_SPACE, TOKEN_WORD, TOKEN_NEWLINE};
        int count = 0;
        int ok = 1;

        while (tokenizer_next(&tok)) {
            token_t *t = tokenizer_current(&tok);
            if (!t || t->type == TOKEN_EOF) break;
            if (count >= 6) { ok = 0; printf("  FAIL: too many tokens\n"); break; }
            const char *type = t->type == TOKEN_WORD ? "WORD" : t->type == TOKEN_SPACE ? "SPACE" : t->type == TOKEN_NEWLINE ? "NEWLINE" : "OTHER";
            printf("  %d: %s '%s' (expected '%s')\n", count, type, t->text, expected[count]);
            if (t->type != expected_types[count] || strcmp(t->text, expected[count]) != 0) {
                ok = 0;
            }
            count++;
        }
        if (count != 6) { ok = 0; printf("  FAIL: got %d tokens, expected 6\n", count); }
        printf("  %s\n", ok ? "PASS" : "FAIL");
        if (!ok) failed++;
        fclose(f);
    }

    // Test 2: Tag with attributes
    printf("Test 2: Tag with attributes\n");
    {
        const char *input = "Before <span id=\"foo\" class=\"bar\">after\n";
        FILE *f = fmemopen((void *)input, strlen(input), "r");
        tokenizer_t tok;
        tokenizer_init(&tok, f);

        const char *expected[] = {"Before", " ", "after", "\n"};
        int count = 0;
        int ok = 1;

        while (tokenizer_next(&tok)) {
            token_t *t = tokenizer_current(&tok);
            if (!t || t->type == TOKEN_EOF) break;
            if (count >= 4) { ok = 0; break; }
            printf("  %d: '%s'\n", count, t->text);
            if (strcmp(t->text, expected[count]) != 0) ok = 0;
            count++;
        }
        if (count != 4) { ok = 0; printf("  FAIL: got %d tokens, expected 4\n", count); }
        printf("  %s\n", ok ? "PASS" : "FAIL");
        if (!ok) failed++;
        fclose(f);
    }

    // Test 3: Multiple consecutive tags
    printf("Test 3: Multiple consecutive tags\n");
    {
        const char *input = "<td><a href=\"#link\">CHAPTER</a></td>\n";
        FILE *f = fmemopen((void *)input, strlen(input), "r");
        tokenizer_t tok;
        tokenizer_init(&tok, f);

        const char *expected[] = {"CHAPTER", "\n"};
        int count = 0;
        int ok = 1;

        while (tokenizer_next(&tok)) {
            token_t *t = tokenizer_current(&tok);
            if (!t || t->type == TOKEN_EOF) break;
            if (count >= 2) { ok = 0; break; }
            printf("  %d: '%s'\n", count, t->text);
            if (strcmp(t->text, expected[count]) != 0) ok = 0;
            count++;
        }
        if (count != 2) { ok = 0; printf("  FAIL: got %d tokens, expected 2\n", count); }
        printf("  %s\n", ok ? "PASS" : "FAIL");
        if (!ok) failed++;
        fclose(f);
    }

    // Test 4: Tag at end of line
    printf("Test 4: Tag at end of line\n");
    {
        const char *input = "Cities</span>\nNext\n";
        FILE *f = fmemopen((void *)input, strlen(input), "r");
        tokenizer_t tok;
        tokenizer_init(&tok, f);

        const char *expected[] = {"Cities", "\n", "Next", "\n"};
        int count = 0;
        int ok = 1;

        while (tokenizer_next(&tok)) {
            token_t *t = tokenizer_current(&tok);
            if (!t || t->type == TOKEN_EOF) break;
            if (count >= 4) { ok = 0; break; }
            printf("  %d: '%s'\n", count, t->text);
            if (strcmp(t->text, expected[count]) != 0) ok = 0;
            count++;
        }
        if (count != 4) { ok = 0; printf("  FAIL: got %d tokens, expected 4\n", count); }
        printf("  %s\n", ok ? "PASS" : "FAIL");
        if (!ok) failed++;
        fclose(f);
    }

    printf("\n=== %s ===\n", failed ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return failed;
}
