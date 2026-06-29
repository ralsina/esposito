#ifndef BB_IO_H
#define BB_IO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void (*write)(const char *data, size_t len, void *userdata);
    void *userdata;
} bb_output_t;

void bb_io_init(void *term);
void bb_write(const char *data, size_t len);
void bb_printf(const char *fmt, ...);
void bb_puts(const char *s);
void bb_putchar(int c);
void bb_flush(void);
void *bb_get_term(void);

// Output redirection for pipes/redirects
void bb_push_output(bb_output_t out);
void bb_pop_output(void);

#endif
