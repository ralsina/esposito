#include <stdio.h>
#include <string.h>
#include "reader_token.h"

int main(void) {
    const char *input = "## The Project Gutenberg eBook of <span id=\"2494373919286673583_98-h-0.htm.html_pg-title-no-subtitle\" lang=\"en\">A Tale of Two Cities</span>\n\n";

    FILE *f = fmemopen((void *)input, strlen(input), "r");
    tokenizer_t tok;
    tokenizer_init(&tok, f);

    int count = 0;
    while (tokenizer_next(&tok)) {
        token_t *t = tokenizer_current(&tok);
        if (!t) break;
        count++;
        const char *type = t->type == TOKEN_WORD ? "WORD" : t->type == TOKEN_HASH ? "HASH" : t->type == TOKEN_NEWLINE ? "NEWLINE" : t->type == TOKEN_SPACE ? "SPACE" : "EOF";
        printf("%3d: %-8s pos=%4ld len=%3zu '%s'\n", count, type, t->file_pos, t->text_len, t->text);
        if (t->type == TOKEN_EOF) break;
    }

    fclose(f);
    return 0;
}
