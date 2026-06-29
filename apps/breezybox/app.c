#include "os_core.h"
#include "terminal_mode.h"
#include "text_mode.h"
#include "hardware.h"
#include "bb_io.h"
#include "bb_vfs.h"
#include "bb_exec.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "breezybox"
#define MAX_LINE 256
#define MAX_HISTORY 20

typedef struct {
    char line[MAX_LINE];
    int cursor;
    int len;
    char saved_line[MAX_LINE];

    char history[MAX_HISTORY][MAX_LINE];
    int history_count;
    int history_idx;

    terminal_mode_t *term;
    char status[80];
    int more_waiting;
} breezybox_t;

static breezybox_t *bb = NULL;

static void refresh_status(void) {
    if (!bb || !bb->term) return;
    snprintf(bb->status, sizeof(bb->status), "%s | Fn+W/S history  ESC exit",
             bb_get_cwd());
    terminal_mode_set_status(bb->term, bb->status);
}

static void show_prompt(void) {
    if (!bb) return;
    bb_write("$ ", 2);
}

static void history_add(const char *line) {
    if (!line || !line[0]) return;
    if (bb->history_count > 0 &&
        strcmp(bb->history[bb->history_count - 1], line) == 0)
        return;

    if (bb->history_count < MAX_HISTORY) {
        strncpy(bb->history[bb->history_count], line, MAX_LINE - 1);
        bb->history[bb->history_count][MAX_LINE - 1] = '\0';
        bb->history_count++;
    } else {
        for (int i = 1; i < MAX_HISTORY; i++) {
            strncpy(bb->history[i - 1], bb->history[i], MAX_LINE - 1);
            bb->history[i - 1][MAX_LINE - 1] = '\0';
        }
        strncpy(bb->history[MAX_HISTORY - 1], line, MAX_LINE - 1);
        bb->history[MAX_HISTORY - 1][MAX_LINE - 1] = '\0';
    }
}

static void execute_line(void) {
    if (!bb) return;

    char *p = bb->line;
    while (*p == ' ') p++;
    if (!*p) {
        bb_write("\n", 1);
        show_prompt();
        bb_flush();
        return;
    }

    history_add(bb->line);

    // Send newline (command text already echoed as typed)
    bb_write("\n", 1);
    bb_exec(bb->line);

    // Reset line buffer
    bb->line[0] = '\0';
    bb->cursor = 0;
    bb->len = 0;
    bb->history_idx = -1;

    show_prompt();
    bb_flush();
}

void app_init(app_context_t *ctx) {
    bb = calloc(1, sizeof(breezybox_t));
    if (!bb) return;

    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER;
    ctx->timer_interval_ms = 50;

    bb->term = calloc(1, terminal_mode_struct_size());
    if (!bb->term) {
        free(bb);
        bb = NULL;
        return;
    }

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows() - 1;  // Reserve last row for status bar

    terminal_mode_init(bb->term, cols, rows, NULL);
    terminal_mode_set_status(bb->term, "BreezyBox shell");

    bb_io_init(bb->term);
    bb_vfs_init();

    refresh_status();

    // Welcome header
    bb_printf("\033[1mBreezyBox\033[0m — Unix-like shell for Esposito\n");
    bb_printf("Type 'help' for commands\n\n");
    show_prompt();
    bb_flush();
    terminal_mode_render(bb->term);
    text_mode_flush();
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    if (!bb) return;

    if (bb->term) {
        terminal_mode_clear_active();
        free(bb->term);
    }
    free(bb);
    bb = NULL;

    display_fill_rect(0, 0, display_get_width(), display_get_height(), 0x0000);
}

static void do_render(void) {
    if (bb && bb->term) {
        terminal_mode_render(bb->term);
        text_mode_flush();
    }
}

void app_event(app_context_t *ctx, event_t *event) {
    if (!bb) return;

    if (event->type == EVENT_TIMER) {
        do_render();
        return;
    }

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) return;

    char key = event->keyboard.key;
    uint8_t mod = event->keyboard.modifiers;
    printf("KEY: key=%d mod=0x%02x raw=0x%02x\n",
           (int)(unsigned char)key, mod, event->keyboard.raw_key_code);

    // History navigation with Fn+W/S
    if (mod & MODIFIER_FN) {
        switch (key) {
            case 'w': case 'W':  // history up
                if (bb->history_idx == -1) {
                    if (bb->history_count > 0) {
                        strncpy(bb->saved_line, bb->line, MAX_LINE - 1);
                        bb->saved_line[MAX_LINE - 1] = '\0';
                        bb->history_idx = bb->history_count - 1;
                        strncpy(bb->line, bb->history[bb->history_idx], MAX_LINE - 1);
                        bb->line[MAX_LINE - 1] = '\0';
                        bb->len = strlen(bb->line);
                        bb->cursor = bb->len;
                    }
                } else if (bb->history_idx > 0) {
                    bb->history_idx--;
                    strncpy(bb->line, bb->history[bb->history_idx], MAX_LINE - 1);
                    bb->line[MAX_LINE - 1] = '\0';
                    bb->len = strlen(bb->line);
                    bb->cursor = bb->len;
                }
                bb_printf("\033[2K\r$ %s", bb->line);
                break;
            case 's': case 'S':  // history down
                if (bb->history_idx >= 0) {
                    bb->history_idx++;
                    if (bb->history_idx >= bb->history_count) {
                        bb->history_idx = -1;
                        strncpy(bb->line, bb->saved_line, MAX_LINE - 1);
                        bb->line[MAX_LINE - 1] = '\0';
                    } else {
                        strncpy(bb->line, bb->history[bb->history_idx], MAX_LINE - 1);
                        bb->line[MAX_LINE - 1] = '\0';
                    }
                    bb->len = strlen(bb->line);
                    bb->cursor = bb->len;
                }
                bb_printf("\033[2K\r$ %s", bb->line);
                break;
        }
        bb_flush();
        return;
    }

    switch (key) {
        case '\n': case '\r':
            execute_line();
            break;

        case '\b': case 127:
            if (bb->cursor > 0) {
                memmove(bb->line + bb->cursor - 1,
                        bb->line + bb->cursor,
                        bb->len - bb->cursor);
                bb->cursor--;
                bb->len--;
                bb->line[bb->len] = '\0';
                bb_printf("\033[D \033[D", 7);
            }
            bb_flush();
            break;

        case 21:  // Ctrl+U — clear line
            bb->line[0] = '\0';
            bb->cursor = 0;
            bb->len = 0;
            bb->history_idx = -1;
            bb_printf("\033[2K\r$ ");
            bb_flush();
            break;

        case 12:  // Ctrl+L — clear screen
            bb_printf("\033[2J\033[H");
            refresh_status();
            show_prompt();
            bb_printf("%s", bb->line);
            bb_flush();
            break;

        default:
            if (key >= 32 && key < 127 && bb->len < MAX_LINE - 1) {
                memmove(bb->line + bb->cursor + 1,
                        bb->line + bb->cursor,
                        bb->len - bb->cursor + 1);
                bb->line[bb->cursor] = key;
                bb->cursor++;
                bb->len++;
                char buf[2] = {key, '\0'};
                bb_write(buf, 1);
            }
	    bb_flush();
		    break;
	}
	do_render();
}
