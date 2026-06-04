#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#define TAG "shell"
#define MAX_LINE 256
#define MAX_HISTORY 20
#define MAX_SCROLLBACK 200
#define COPY_BUF 512

typedef struct {
    char line[MAX_LINE];
    int cursor;
    int len;
    char saved_line[MAX_LINE];

    char history[MAX_HISTORY][MAX_LINE];
    int history_count;
    int history_idx;

    char cwd[256];

    char scrollback[MAX_SCROLLBACK][TEXT_MODE_COLS];
    int scrollback_count;
} shell_t;

static shell_t *shell;

static void refresh_all(void);
static void refresh_input(void);

static void sb_add(const char *s) {
    int idx = shell->scrollback_count % MAX_SCROLLBACK;
    strncpy(shell->scrollback[idx], s, TEXT_MODE_COLS - 1);
    shell->scrollback[idx][TEXT_MODE_COLS - 1] = '\0';
    shell->scrollback_count++;
}

static void sb_clear(void) {
    shell->scrollback_count = 0;
}

static void path_normalize(char *path) {
    if (!path || !path[0]) return;
    char *dst = path, *src = path;
    int has_leading = (*src == '/');
    if (has_leading) *dst++ = '/', src++;
    while (*src) {
        while (*src == '/') src++;
        if (!*src) break;
        char *seg = src;
        while (*src && *src != '/') src++;
        size_t seg_len = src - seg;
        if (seg_len == 1 && seg[0] == '.') continue;
        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
            if (dst > path + has_leading) {
                dst--;
                while (dst > path && *dst != '/') dst--;
            }
            continue;
        }
        if (dst > path + has_leading) *dst++ = '/';
        memmove(dst, seg, seg_len);
        dst += seg_len;
    }
    *dst = '\0';
    if (dst == path) strcpy(path, ".");
}

static void path_resolve(const char *input, char *out, size_t out_size) {
    if (!input || !input[0]) {
        strncpy(out, shell->cwd, out_size);
        out[out_size - 1] = '\0';
        return;
    }
    if (input[0] == '/') {
        strncpy(out, input, out_size);
        out[out_size - 1] = '\0';
    } else {
        snprintf(out, out_size, "%s/%s", shell->cwd, input);
    }
    path_normalize(out);
}

static void cmd_ls(const char *arg) {
    char path[256];
    path_resolve(arg, path, sizeof(path));

    DIR *dir = opendir(path);
    if (!dir) {
        char errmsg[65];
        snprintf(errmsg, sizeof(errmsg), "ls: cannot open '%s'", path);
        sb_add(errmsg);
        return;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char full[256];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        struct stat st;
        char line[TEXT_MODE_COLS];
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(line, sizeof(line), "  %s/", entry->d_name);
        } else {
            snprintf(line, sizeof(line), "  %s", entry->d_name);
        }
        sb_add(line);
        count++;
    }
    closedir(dir);

    char total[32];
    snprintf(total, sizeof(total), "total: %d", count);
    sb_add(total);
}

static void cmd_cd(const char *arg) {
    char new_cwd[256];

    if (!arg) {
        strncpy(new_cwd, "/sdcard", sizeof(new_cwd) - 1);
        new_cwd[sizeof(new_cwd) - 1] = '\0';
    } else if (strcmp(arg, "..") == 0) {
        strncpy(new_cwd, shell->cwd, sizeof(new_cwd) - 1);
        new_cwd[sizeof(new_cwd) - 1] = '\0';
        char *slash = strrchr(new_cwd, '/');
        if (slash && slash != new_cwd) {
            *slash = '\0';
        } else {
            new_cwd[1] = '\0';
        }
    } else {
        path_resolve(arg, new_cwd, sizeof(new_cwd));
    }

    struct stat st;
    if (stat(new_cwd, &st) != 0) {
        sb_add("cd: no such directory");
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        sb_add("cd: not a directory");
        return;
    }

    size_t len = strlen(new_cwd);
    while (len > 1 && new_cwd[len - 1] == '/') {
        new_cwd[len - 1] = '\0';
        len--;
    }

    strncpy(shell->cwd, new_cwd, sizeof(shell->cwd) - 1);
    shell->cwd[sizeof(shell->cwd) - 1] = '\0';
}

static void cmd_cp(const char *src, const char *dst) {
    if (!src || !dst) {
        sb_add("usage: cp <src> <dst>");
        return;
    }

    char src_path[256], dst_path[256];
    path_resolve(src, src_path, sizeof(src_path));
    path_resolve(dst, dst_path, sizeof(dst_path));

    FILE *in = fopen(src_path, "r");
    if (!in) {
        sb_add("cp: cannot open source");
        return;
    }

    FILE *out = fopen(dst_path, "w");
    if (!out) {
        fclose(in);
        sb_add("cp: cannot create destination");
        return;
    }

    char buf[COPY_BUF];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }

    fclose(in);
    fclose(out);
    sb_add("cp: done");
}

static void cmd_mv(const char *src, const char *dst) {
    if (!src || !dst) {
        sb_add("usage: mv <src> <dst>");
        return;
    }

    char src_path[256], dst_path[256];
    path_resolve(src, src_path, sizeof(src_path));
    path_resolve(dst, dst_path, sizeof(dst_path));

    if (rename(src_path, dst_path) == 0) {
        sb_add("mv: done");
    } else {
        sb_add("mv: failed");
    }
}

static void history_add(const char *line) {
    if (!line || !line[0]) return;
    if (shell->history_count > 0 &&
        strcmp(shell->history[shell->history_count - 1], line) == 0)
        return;

    if (shell->history_count < MAX_HISTORY) {
        strncpy(shell->history[shell->history_count], line, MAX_LINE - 1);
        shell->history[shell->history_count][MAX_LINE - 1] = '\0';
        shell->history_count++;
    } else {
        for (int i = 1; i < MAX_HISTORY; i++) {
            strncpy(shell->history[i - 1], shell->history[i], MAX_LINE - 1);
            shell->history[i - 1][MAX_LINE - 1] = '\0';
        }
        strncpy(shell->history[MAX_HISTORY - 1], line, MAX_LINE - 1);
        shell->history[MAX_HISTORY - 1][MAX_LINE - 1] = '\0';
    }
}

static void execute(char *cmd) {
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    char full_cmd[MAX_LINE];
    strncpy(full_cmd, cmd, sizeof(full_cmd) - 1);
    full_cmd[sizeof(full_cmd) - 1] = '\0';

    char *args[10];
    int argc = 0;
    {
        char *p = cmd;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            if (argc >= 10) break;
            args[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) { *p = '\0'; p++; }
        }
    }
    if (argc == 0) return;

    history_add(full_cmd);

    {
        char echo_buf[TEXT_MODE_COLS];
        snprintf(echo_buf, sizeof(echo_buf), "$ %s", full_cmd);
        sb_add(echo_buf);
    }

    if (strcmp(args[0], "ls") == 0) {
        cmd_ls(argc > 1 ? args[1] : ".");
    } else if (strcmp(args[0], "cd") == 0) {
        cmd_cd(argc > 1 ? args[1] : NULL);
    } else if (strcmp(args[0], "cp") == 0) {
        cmd_cp(argc > 1 ? args[1] : NULL, argc > 2 ? args[2] : NULL);
    } else if (strcmp(args[0], "mv") == 0) {
        cmd_mv(argc > 1 ? args[1] : NULL, argc > 2 ? args[2] : NULL);
    } else if (argc >= 2) {
        os_open_app_with_file(args[0], args[1]);
        return;
    } else {
        char msg[TEXT_MODE_COLS];
        snprintf(msg, sizeof(msg), "shell: unknown: %s", args[0]);
        sb_add(msg);
    }

    shell->line[0] = '\0';
    shell->cursor = 0;
    shell->len = 0;
    shell->history_idx = -1;

    refresh_all();
}

static void refresh_all(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int scroll_rows = rows - 5;
    int sep_row = scroll_rows;
    int status_row = scroll_rows + 1;

    text_mode_clear(TEXT_COLOR_BLACK);

    {
        int total = shell->scrollback_count;
        int start = total > scroll_rows ? total - scroll_rows : 0;
        for (int r = 0; r < scroll_rows; r++) {
            int idx = start + r;
            if (idx < total) {
                text_mode_print_at(0, r, shell->scrollback[idx % MAX_SCROLLBACK]);
            }
        }
    }

    {
        char sep[65];
        int n = cols < 64 ? cols : 64;
        memset(sep, '=', n);
        sep[n] = '\0';
        text_mode_print_at_attr(0, sep_row, sep, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    }

    {
        int cwd_len = strlen(shell->cwd);
        int max_cwd = cols - 6;
        if (cwd_len > max_cwd) {
            text_mode_printf_at(0, status_row, "CWD: ..%s",
                                shell->cwd + cwd_len - max_cwd + 3);
        } else {
            text_mode_printf_at(0, status_row, "CWD: %s", shell->cwd);
        }
    }

    refresh_input();
    text_mode_flush();
}

static void refresh_input(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int input_row = rows - 3;
    int prompt_len = 2;
    int max_chars = cols - prompt_len;

    {
        char spaces[65];
        int n = cols < 64 ? cols : 64;
        memset(spaces, ' ', n);
        spaces[n] = '\0';
        text_mode_print_at(0, input_row, spaces);
    }

    text_mode_print_at(0, input_row, "$ ");

    int display_len = shell->len;
    int offset = 0;
    if (display_len > max_chars) {
        offset = display_len - max_chars;
        display_len = max_chars;
    }

    if (display_len > 0) {
        char buf[65];
        int n = display_len < 64 ? display_len : 64;
        strncpy(buf, shell->line + offset, n);
        buf[n] = '\0';
        text_mode_print_at(prompt_len, input_row, buf);
    }

    {
        int cursor_col = shell->cursor - offset;
        if (cursor_col < 0) cursor_col = 0;
        if (cursor_col > max_chars) cursor_col = max_chars;

        if (cursor_col < display_len) {
            char c[2] = {shell->line[offset + cursor_col], '\0'};
            text_mode_print_at_attr_bg(prompt_len + cursor_col, input_row, c,
                                       TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
        } else if (cursor_col < max_chars - 1) {
            text_mode_print_at_attr_bg(prompt_len + cursor_col, input_row, " ",
                                       TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
        }
    }
}

void app_init(app_context_t *ctx) {
    shell = malloc(sizeof(shell_t));
    if (!shell) return;
    memset(shell, 0, sizeof(shell_t));

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    text_mode_init();

    config_get_string("cwd", "/sdcard", shell->cwd, sizeof(shell->cwd));
    shell->history_count = config_get_int("hcount", 0);
    if (shell->history_count > MAX_HISTORY) shell->history_count = MAX_HISTORY;
    for (int i = 0; i < shell->history_count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "h_%d", i);
        config_get_string(key, "", shell->history[i], sizeof(shell->history[i]));
    }
    shell->history_idx = -1;

    sb_add("Esposito Shell");
    sb_add("builtins: ls cd cp mv");
    sb_add("type <app> <file> to launch");

    refresh_all();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (!shell) return;
    if (event->type != EVENT_KEYBOARD) return;
    if (!event->keyboard.pressed) return;

    char key = event->keyboard.key;
    uint8_t mod = event->keyboard.modifiers;

    if (mod & MODIFIER_FN) {
        switch (key) {
            case 'a': case 'A':
                if (shell->cursor > 0) shell->cursor--;
                break;
            case 'd': case 'D':
                if (shell->cursor < shell->len) shell->cursor++;
                break;
            case 'w': case 'W':
                if (shell->history_idx == -1) {
                    if (shell->history_count > 0) {
                        strncpy(shell->saved_line, shell->line, MAX_LINE - 1);
                        shell->saved_line[MAX_LINE - 1] = '\0';
                        shell->history_idx = shell->history_count - 1;
                        strncpy(shell->line, shell->history[shell->history_idx], MAX_LINE - 1);
                        shell->line[MAX_LINE - 1] = '\0';
                        shell->len = strlen(shell->line);
                        shell->cursor = shell->len;
                    }
                } else if (shell->history_idx > 0) {
                    shell->history_idx--;
                    strncpy(shell->line, shell->history[shell->history_idx], MAX_LINE - 1);
                    shell->line[MAX_LINE - 1] = '\0';
                    shell->len = strlen(shell->line);
                    shell->cursor = shell->len;
                }
                break;
            case 's': case 'S':
                if (shell->history_idx >= 0) {
                    shell->history_idx++;
                    if (shell->history_idx >= shell->history_count) {
                        shell->history_idx = -1;
                        strncpy(shell->line, shell->saved_line, MAX_LINE - 1);
                        shell->line[MAX_LINE - 1] = '\0';
                    } else {
                        strncpy(shell->line, shell->history[shell->history_idx], MAX_LINE - 1);
                        shell->line[MAX_LINE - 1] = '\0';
                    }
                    shell->len = strlen(shell->line);
                    shell->cursor = shell->len;
                }
                break;
        }
        refresh_input();
        text_mode_flush();
        return;
    }

    switch (key) {
        case '\n': case '\r':
            execute(shell->line);
            break;

        case '\b': case 127:
            if (shell->cursor > 0) {
                memmove(shell->line + shell->cursor - 1,
                        shell->line + shell->cursor,
                        shell->len - shell->cursor);
                shell->cursor--;
                shell->len--;
                shell->line[shell->len] = '\0';
            }
            refresh_input();
            text_mode_flush();
            break;

        case 21:
            shell->line[0] = '\0';
            shell->cursor = 0;
            shell->len = 0;
            shell->history_idx = -1;
            refresh_input();
            text_mode_flush();
            break;

        case 12:
            sb_clear();
            refresh_all();
            break;

        default:
            if (key >= 32 && key < 127 && shell->len < MAX_LINE - 1) {
                memmove(shell->line + shell->cursor + 1,
                        shell->line + shell->cursor,
                        shell->len - shell->cursor + 1);
                shell->line[shell->cursor] = key;
                shell->cursor++;
                shell->len++;
                refresh_input();
                text_mode_flush();
            }
            break;
    }
}

void app_checkpoint(app_context_t *ctx) {
    if (!shell) return;

    config_set_string("cwd", shell->cwd);
    config_set_int("hcount", shell->history_count);
    for (int i = 0; i < shell->history_count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "h_%d", i);
        config_set_string(key, shell->history[i]);
    }
}

void app_close(app_context_t *ctx) {
    if (!shell) return;

    config_set_string("cwd", shell->cwd);
    config_set_int("hcount", shell->history_count);
    for (int i = 0; i < shell->history_count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "h_%d", i);
        config_set_string(key, shell->history[i]);
    }

    free(shell);
    shell = NULL;

    text_mode_clear(TEXT_COLOR_BLACK);
}
