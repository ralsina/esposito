#ifndef TERMINAL_MODE_H
#define TERMINAL_MODE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "os_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct terminal_mode terminal_mode_t;

typedef void (*terminal_mode_write_cb)(const char *data, size_t len);
typedef void (*terminal_mode_title_cb)(const char *title);

terminal_mode_t *terminal_mode_default(void);

bool terminal_mode_init(terminal_mode_t *term, int cols, int rows, terminal_mode_write_cb write_cb);
void terminal_mode_reset(terminal_mode_t *term);

void terminal_mode_set_write_callback(terminal_mode_t *term, terminal_mode_write_cb cb);
void terminal_mode_set_title_callback(terminal_mode_t *term, terminal_mode_title_cb cb);

void terminal_mode_process_bytes(terminal_mode_t *term, const char *data, size_t len);
void terminal_mode_handle_key(terminal_mode_t *term, char key, uint8_t modifiers);

void terminal_mode_set_status(terminal_mode_t *term, const char *status);
void terminal_mode_render(terminal_mode_t *term);

int terminal_mode_cols(const terminal_mode_t *term);
int terminal_mode_rows(const terminal_mode_t *term);

#ifdef __cplusplus
}
#endif

#endif
