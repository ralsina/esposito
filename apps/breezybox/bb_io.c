#include "bb_io.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern void terminal_mode_process_bytes(void *term, const char *data, size_t len);

static void *g_term = NULL;
static bb_output_t s_output_stack[8];
static int s_output_depth = 0;

static void term_write(const char *data, size_t len, void *userdata) {
    (void)userdata;
    if (!data || len == 0) return;

    // Convert \n to \r\n for strict VT100 compliance
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n') {
            if (i > start) {
                terminal_mode_process_bytes(g_term, data + start, i - start);
            }
            terminal_mode_process_bytes(g_term, "\r\n", 2);
            start = i + 1;
        }
    }
    if (start < len) {
        terminal_mode_process_bytes(g_term, data + start, len - start);
    }
}

void bb_io_init(void *term) {
    g_term = term;
    s_output_depth = 0;
    s_output_stack[0].write = term_write;
    s_output_stack[0].userdata = NULL;
    s_output_depth = 1;
}

void bb_write(const char *data, size_t len) {
    if (s_output_depth > 0 && data && len > 0) {
        bb_output_t *out = &s_output_stack[s_output_depth - 1];
        out->write(data, len, out->userdata);
    }
}

void bb_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        bb_write(buf, len);
    }
}

void bb_puts(const char *s) {
    if (s) {
        bb_write(s, strlen(s));
        bb_write("\n", 1);
    }
}

void bb_putchar(int c) {
    char ch = (char)c;
    bb_write(&ch, 1);
}

void bb_flush(void) {
}

void *bb_get_term(void) {
    return g_term;
}

void bb_push_output(bb_output_t out) {
    if (s_output_depth < 8) {
        s_output_stack[s_output_depth++] = out;
    }
}

void bb_pop_output(void) {
    if (s_output_depth > 1) {
        s_output_depth--;
    }
}
