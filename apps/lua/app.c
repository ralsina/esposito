#include "os_core.h"
#include "text_mode.h"
#include "lua_bindings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INPUT_LEN 256
#define SCROLLBACK 100

static lua_State *L = NULL;
static char input[INPUT_LEN];
static int input_pos = 0;
static char scrollback[SCROLLBACK][INPUT_LEN];
static int sb_count = 0;
static bool lua_ready = false;

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return NULL; }
    return realloc(ptr, nsize);
}

static void sb_push(const char *s) {
    if (sb_count < SCROLLBACK) {
        strncpy(scrollback[sb_count], s, INPUT_LEN - 1);
        scrollback[sb_count][INPUT_LEN - 1] = '\0';
        sb_count++;
    } else {
        for (int i = 0; i < SCROLLBACK - 1; i++)
            strcpy(scrollback[i], scrollback[i + 1]);
        strncpy(scrollback[SCROLLBACK - 1], s, INPUT_LEN - 1);
        scrollback[SCROLLBACK - 1][INPUT_LEN - 1] = '\0';
    }
}

static void redraw(void) {
    int rows = text_mode_get_rows();
    int disp_rows = rows - 1;

    text_mode_clear(TEXT_COLOR_BLACK);
    int top = sb_count > disp_rows ? sb_count - disp_rows : 0;
    int n = sb_count - top;
    if (n > disp_rows) n = disp_rows;
    for (int i = 0; i < n; i++)
        text_mode_print_at_color(0, i, scrollback[top + i], TEXT_COLOR_BRIGHT_WHITE);
    char prompt[INPUT_LEN + 4];
    snprintf(prompt, sizeof(prompt), "> %s", input);
    text_mode_print_at_color(0, rows - 1, prompt, TEXT_COLOR_BRIGHT_GREEN);
    text_mode_flush();
}

static void eval_line(const char *line) {
    char line_label[INPUT_LEN + 4];
    snprintf(line_label, sizeof(line_label), "> %s", line);
    sb_push(line_label);

    if (!lua_ready) {
        sb_push("ERR: Lua not initialized");
        return;
    }

    char buf[INPUT_LEN + 16];
    snprintf(buf, sizeof(buf), "return %s", line);
    int res = luaL_loadstring(L, buf);
    if (res != LUA_OK) {
        lua_settop(L, 0);
        res = luaL_loadstring(L, line);
    }
    if (res != LUA_OK) {
        char err[INPUT_LEN + 8];
        snprintf(err, sizeof(err), "ERR: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        sb_push(err);
        return;
    }
    res = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (res != LUA_OK) {
        char err[INPUT_LEN + 8];
        snprintf(err, sizeof(err), "ERR: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        sb_push(err);
        return;
    }
    int n = lua_gettop(L);
    if (n == 0) {
        sb_push("(no result)");
    } else {
        for (int i = 1; i <= n; i++) {
            const char *s = lua_tostring(L, i);
            if (s) sb_push(s);
            else sb_push(lua_typename(L, lua_type(L, i)));
        }
    }
    lua_settop(L, 0);
}

static void run_script(const char *path) {
    if (!lua_ready) return;
    char label[INPUT_LEN];
    snprintf(label, sizeof(label), "Running: %s", path);
    sb_push(label);

    FILE *f = fopen(path, "r");
    if (!f) { sb_push("ERR: cannot open file"); return; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); sb_push("ERR: empty file"); return; }
    char *code = malloc(len + 1);
    if (!code) { fclose(f); sb_push("ERR: out of memory"); return; }
    size_t nread = fread(code, 1, len, f);
    fclose(f);
    code[nread] = '\0';

    int res = luaL_loadstring(L, code);
    free(code);
    if (res != LUA_OK) {
        char err[INPUT_LEN + 8];
        snprintf(err, sizeof(err), "ERR: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        sb_push(err);
        return;
    }
    res = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (res != LUA_OK) {
        char err[INPUT_LEN + 8];
        snprintf(err, sizeof(err), "ERR: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        sb_push(err);
        return;
    }
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        const char *s = lua_tostring(L, i);
        if (s) sb_push(s);
        else sb_push(lua_typename(L, lua_type(L, i)));
    }
    lua_settop(L, 0);
    sb_push("--- script done ---");
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER;
    ctx->timer_interval_ms = 50;

    text_mode_init();
    text_mode_clear(TEXT_COLOR_BLACK);

    sb_push("Lua 5.4 REPL");
    sb_push("Initializing Lua...");

    L = lua_newstate(lua_alloc, NULL);
    if (!L) {
        sb_push("ERR: lua_newstate failed");
        redraw();
        return;
    }

    sb_push("Opening standard libs...");
    redraw();

    luaL_openlibs(L);
    lua_open_esposito_libs(L);
    lua_ready = true;

    char startup_path[128];
    size_t path_len = os_consume_startup_file(startup_path, sizeof(startup_path));
    if (path_len > 0)
        run_script(startup_path);

    sb_push("Ready. Ctrl+ESC for launcher.");
    redraw();
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;
    if (event->type == EVENT_TIMER) return;

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char c = event->keyboard.key;
        int mod = event->keyboard.modifiers;

        if (c >= 32 && c <= 126 && input_pos < INPUT_LEN - 1) {
            input[input_pos++] = c;
            input[input_pos] = '\0';
        } else if (c == 127 || c == '\b') {
            if (input_pos > 0) input[--input_pos] = '\0';
        } else if (c == '\r' || c == '\n') {
            if (input_pos > 0) {
                input[input_pos] = '\0';
                char copy[INPUT_LEN];
                strcpy(copy, input);
                input[0] = '\0';
                input_pos = 0;
                if (strcmp(copy, "exit") == 0 || strcmp(copy, "quit") == 0) {
                    os_load_app("launcher");
                    return;
                }
                eval_line(copy);
            }
        }
        redraw();
    }
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    if (L) { lua_close(L); L = NULL; }
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_flush();
}
