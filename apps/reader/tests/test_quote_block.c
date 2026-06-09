#include "reader_token.h"
#include "reader_types.h"
#include "reader_renderer.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define LINE_BUF_SIZE (53 + LINE_BUF_MARGIN)

static void setup_lines(rendered_line_t *lines) {
    char *text_buf = malloc((size_t)MAX_RENDERED_LINES * LINE_BUF_SIZE);
    for (int i = 0; i < MAX_RENDERED_LINES; i++) {
        lines[i].text = text_buf + (size_t)i * LINE_BUF_SIZE;
        lines[i].text[0] = '\0';
        lines[i].color = 0;
        lines[i].attr = 0;
    }
}

static void teardown_lines(rendered_line_t *lines) {
    if (lines[0].text) {
        free(lines[0].text);
    }
}

int main(void) {
    int failed = 0;

    // Test 1: Simple quote block
    printf("Test 1: Simple quote block\n");
    {
        const char *input = "> This is a quote\n\nNormal text\n";
        FILE *f = fmemopen((void *)input, strlen(input), "r");
        rendered_line_t lines[MAX_RENDERED_LINES];
        uint8_t heading_levels[MAX_RENDERED_LINES];
        memset(heading_levels, 0, sizeof(heading_levels));
        setup_lines(lines);
        page_renderer_t renderer;
        renderer_init(&renderer, f, lines, heading_levels, 18, 53, LINE_BUF_SIZE);
        renderer_process_page(&renderer);

        printf("  Rendered %d lines:\n", renderer.line_count);
        for (int i = 0; i < renderer.line_count; i++) {
            printf("  Line %d [color=%d]: '%s'\n", i, lines[i].color, lines[i].text);
        }

        // Line 0 should be the quote text with a quote indicator
        if (renderer.line_count < 3) {
            printf("  FAIL: expected at least 3 lines, got %d\n", renderer.line_count);
            failed++;
        } else if (lines[0].text[0] != ' ' || lines[0].text[1] != ' ') {
            printf("  FAIL: quote line should start with 2 spaces, starts with '%c%c'\n", lines[0].text[0], lines[0].text[1]);
            failed++;
        } else if (strstr(lines[0].text, "This is a quote") == NULL) {
            printf("  FAIL: quote line should contain 'This is a quote'\n");
            failed++;
        } else if (strstr(lines[2].text, "Normal text") == NULL) {
            printf("  FAIL: line 2 should contain 'Normal text'\n");
            failed++;
        } else {
            printf("  PASS\n");
        }
        fclose(f);
        teardown_lines(lines);
    }

    // Test 2: Multi-line quote block
    printf("Test 2: Multi-line quote block\n");
    {
        const char *input = "> Line one\n> Line two\n\nAfter quote\n";
        FILE *f = fmemopen((void *)input, strlen(input), "r");
        rendered_line_t lines[MAX_RENDERED_LINES];
        uint8_t heading_levels[MAX_RENDERED_LINES];
        memset(heading_levels, 0, sizeof(heading_levels));
        setup_lines(lines);
        page_renderer_t renderer;
        renderer_init(&renderer, f, lines, heading_levels, 18, 53, LINE_BUF_SIZE);
        renderer_process_page(&renderer);

        printf("  Rendered %d lines:\n", renderer.line_count);
        for (int i = 0; i < renderer.line_count; i++) {
            printf("  Line %d [color=%d]: '%s'\n", i, lines[i].color, lines[i].text);
        }

        if (renderer.line_count < 4) {
            printf("  FAIL: expected at least 4 lines, got %d\n", renderer.line_count);
            failed++;
        } else if (strstr(lines[0].text, "Line one") == NULL) {
            printf("  FAIL: line 0 should contain 'Line one'\n");
            failed++;
        } else if (strstr(lines[1].text, "Line two") == NULL) {
            printf("  FAIL: line 1 should contain 'Line two'\n");
            failed++;
        } else if (strstr(lines[3].text, "After quote") == NULL) {
            printf("  FAIL: line 3 should contain 'After quote'\n");
            failed++;
        } else {
            printf("  PASS\n");
        }
        fclose(f);
        teardown_lines(lines);
    }

    // Test 3: Quote block lines should be visually distinct (different color)
    printf("Test 3: Quote block color\n");
    {
        const char *input = "> Quoted text\n\nNormal text\n";
        FILE *f = fmemopen((void *)input, strlen(input), "r");
        rendered_line_t lines[MAX_RENDERED_LINES];
        uint8_t heading_levels[MAX_RENDERED_LINES];
        memset(heading_levels, 0, sizeof(heading_levels));
        setup_lines(lines);
        page_renderer_t renderer;
        renderer_init(&renderer, f, lines, heading_levels, 18, 53, LINE_BUF_SIZE);
        renderer_process_page(&renderer);

        if (renderer.line_count < 2) {
            printf("  FAIL: expected at least 2 lines\n");
            failed++;
        } else if (lines[0].color == lines[1].color) {
            printf("  FAIL: quote and normal lines have same color (%d)\n", lines[0].color);
            failed++;
        } else {
            printf("  PASS: quote color=%d, normal color=%d\n", lines[0].color, lines[1].color);
        }
        fclose(f);
        teardown_lines(lines);
    }

    printf("\n=== %s ===\n", failed ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return failed;
}
