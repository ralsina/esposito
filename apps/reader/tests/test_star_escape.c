#include "reader_token.h"
#include "reader_types.h"
#include "reader_renderer.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

int main(void) {
    const char *input = "\\*\\*\\* START OF THE PROJECT GUTENBERG EBOOK A TALE OF TWO CITIES \\*\\*\\*\n";

    FILE *f = fmemopen((void *)input, strlen(input), "r");
    rendered_line_t lines[MAX_RENDERED_LINES];
    uint8_t heading_levels[MAX_RENDERED_LINES];
    memset(lines, 0, sizeof(lines));
    memset(heading_levels, 0, sizeof(heading_levels));

    page_renderer_t renderer;
    renderer_init(&renderer, f, lines, heading_levels, 18, 53);
    renderer_process_page(&renderer);

    printf("Rendered %d lines:\n", renderer.line_count);
    for (int i = 0; i < renderer.line_count; i++) {
        printf("Line %2d: '", i);
        for (int j = 0; lines[i].text[j]; j++) {
            unsigned char c = (unsigned char)lines[i].text[j];
            if (c == MD_FORMAT_BOLD) printf("[BOLD]");
            else if (c == MD_FORMAT_TOGGLE) printf("[ITALIC]");
            else if (c == MD_FORMAT_UNDERLINE) printf("[ULINE]");
            else putchar(c);
        }
        printf("'\n");
    }

    printf("\nTest: Line 0 must not start with space\n");
    if (renderer.line_count < 1) {
        printf("FAIL: no lines rendered\n");
        fclose(f);
        return 1;
    }
    if (lines[0].text[0] == ' ') {
        printf("FAIL: line starts with space\n");
        fclose(f);
        return 1;
    }
    printf("PASS: line does not start with space\n");
    fclose(f);
    return 0;
}
