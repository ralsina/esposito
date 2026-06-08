#include "lua_bindings.h"
#include "text_mode.h"
#include "os_core.h"
#include "wifi.h"
#include "hardware.h"
#include <string.h>
#include <stdio.h>

// --- Display functions ---

static int l_esposito_clear(lua_State *L) {
    int bg = luaL_optinteger(L, 1, TEXT_COLOR_BLACK);
    text_mode_clear(bg);
    return 0;
}

static int l_esposito_print(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char *str = luaL_checkstring(L, 3);
    text_mode_print_at(x, y, str);
    return 0;
}

static int l_esposito_print_color(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char *str = luaL_checkstring(L, 3);
    int color = luaL_optinteger(L, 4, TEXT_COLOR_WHITE);
    text_mode_print_at_color(x, y, str, color);
    return 0;
}

static int l_esposito_print_attr(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char *str = luaL_checkstring(L, 3);
    int fg = luaL_optinteger(L, 4, TEXT_COLOR_WHITE);
    int attr = luaL_optinteger(L, 5, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(x, y, str, fg, attr);
    return 0;
}

static int l_esposito_flush(lua_State *L) {
    (void)L;
    text_mode_flush();
    return 0;
}

static int l_esposito_get_cols(lua_State *L) {
    lua_pushinteger(L, text_mode_get_cols());
    return 1;
}

static int l_esposito_get_rows(lua_State *L) {
    lua_pushinteger(L, text_mode_get_rows());
    return 1;
}

static int l_esposito_set_cursor(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    text_mode_set_cursor(x, y);
    return 0;
}

static int l_esposito_get_cursor(lua_State *L) {
    int x, y;
    text_mode_get_cursor(&x, &y);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    return 2;
}

static int l_esposito_fill_rect(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    int color = luaL_optinteger(L, 5, TEXT_COLOR_WHITE);
    display_fill_rect(x, y, w, h, color);
    return 0;
}

static int l_esposito_draw_pixel(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int color = luaL_optinteger(L, 3, TEXT_COLOR_WHITE);
    display_draw_pixel(x, y, color);
    return 0;
}

static int l_esposito_get_size(lua_State *L) {
    lua_pushinteger(L, display_get_width());
    lua_pushinteger(L, display_get_height());
    return 2;
}

// --- Input functions ---

static int l_esposito_keyboard_read(lua_State *L) {
    event_t event;
    if (!keyboard_read_event(&event)) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    char key_str[2] = { event.keyboard.key, '\0' };
    lua_pushstring(L, key_str);
    lua_pushboolean(L, event.keyboard.pressed);
    lua_pushinteger(L, event.keyboard.modifiers);
    lua_pushinteger(L, event.keyboard.raw_key_code);
    return 4;
}

// --- System functions ---

static int l_esposito_time(lua_State *L) {
    os_time_status_t status;
    if (!os_get_time_status(&status)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, status.unix_time);
    lua_setfield(L, -2, "unix_time");
    lua_pushinteger(L, status.year);
    lua_setfield(L, -2, "year");
    lua_pushinteger(L, status.month);
    lua_setfield(L, -2, "month");
    lua_pushinteger(L, status.day);
    lua_setfield(L, -2, "day");
    lua_pushinteger(L, status.hour);
    lua_setfield(L, -2, "hour");
    lua_pushinteger(L, status.minute);
    lua_setfield(L, -2, "minute");
    lua_pushinteger(L, status.second);
    lua_setfield(L, -2, "second");
    lua_pushboolean(L, status.synchronized);
    lua_setfield(L, -2, "synced");
    return 1;
}

static int l_esposito_load_app(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    bool ok = os_load_app(name);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_esposito_settings_get(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    const char *default_val = luaL_optstring(L, 2, "");
    char buf[256];
    size_t len = os_settings_get_string(key, default_val, buf, sizeof(buf));
    if (len > 0) {
        lua_pushstring(L, buf);
    } else {
        lua_pushstring(L, default_val);
    }
    return 1;
}

static int l_esposito_settings_set(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    const char *value = luaL_checkstring(L, 2);
    bool ok = os_settings_set_string(key, value);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_esposito_settings_get_int(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    int default_val = luaL_optinteger(L, 2, 0);
    int val = os_settings_get_int(key, default_val);
    lua_pushinteger(L, val);
    return 1;
}

static int l_esposito_settings_set_int(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    int value = luaL_checkinteger(L, 2);
    bool ok = os_settings_set_int(key, value);
    lua_pushboolean(L, ok);
    return 1;
}

// --- Networking functions ---

static int l_esposito_wifi_connect(lua_State *L) {
    const char *ssid = luaL_checkstring(L, 1);
    const char *password = luaL_optstring(L, 2, "");
    bool ok = wifi_connect(ssid, password);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_esposito_wifi_is_connected(lua_State *L) {
    (void)L;
    lua_pushboolean(L, wifi_is_connected());
    return 1;
}

static int l_esposito_wifi_scan(lua_State *L) {
    (void)L;
    int count = wifi_scan();
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; i++) {
        lua_createtable(L, 0, 2);
        lua_pushstring(L, wifi_scan_get_ssid(i));
        lua_setfield(L, -2, "ssid");
        lua_pushinteger(L, wifi_scan_get_rssi(i));
        lua_setfield(L, -2, "rssi");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_esposito_http_get(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    int timeout = luaL_optinteger(L, 2, 5000);
    char buf[2048];
    int result = os_http_get(url, buf, sizeof(buf), timeout);
    if (result < 0) {
        lua_pushnil(L);
        lua_pushinteger(L, result);
        return 2;
    }
    lua_pushlstring(L, buf, result);
    lua_pushnil(L);
    return 2;
}

// --- Draw text in graphics mode ---

static int l_esposito_draw_text(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    int color = luaL_optinteger(L, 4, TEXT_COLOR_WHITE);
    display_draw_text(x, y, text, color);
    return 0;
}

static int l_esposito_draw_scaled_text(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    int fg = luaL_optinteger(L, 4, TEXT_COLOR_WHITE);
    int bg = luaL_optinteger(L, 5, TEXT_COLOR_BLACK);
    int scale = luaL_optinteger(L, 6, 1);
    display_draw_scaled_text_bg(x, y, text, fg, bg, scale);
    return 0;
}

// --- Module registration ---

static const struct luaL_Reg esposito_funcs[] = {
    // Display
    {"clear",           l_esposito_clear},
    {"print",           l_esposito_print},
    {"print_color",     l_esposito_print_color},
    {"print_attr",      l_esposito_print_attr},
    {"flush",           l_esposito_flush},
    {"get_cols",        l_esposito_get_cols},
    {"get_rows",        l_esposito_get_rows},
    {"set_cursor",      l_esposito_set_cursor},
    {"get_cursor",      l_esposito_get_cursor},
    {"fill_rect",       l_esposito_fill_rect},
    {"draw_pixel",      l_esposito_draw_pixel},
    {"get_size",        l_esposito_get_size},
    {"draw_text",       l_esposito_draw_text},
    {"draw_scaled_text", l_esposito_draw_scaled_text},
    // Input
    {"keyboard_read",   l_esposito_keyboard_read},
    // System
    {"time",            l_esposito_time},
    {"load_app",        l_esposito_load_app},
    {"settings_get",    l_esposito_settings_get},
    {"settings_set",    l_esposito_settings_set},
    {"settings_get_int", l_esposito_settings_get_int},
    {"settings_set_int", l_esposito_settings_set_int},
    // Networking
    {"wifi_connect",    l_esposito_wifi_connect},
    {"wifi_is_connected", l_esposito_wifi_is_connected},
    {"wifi_scan",       l_esposito_wifi_scan},
    {"http_get",        l_esposito_http_get},
    {NULL, NULL}
};

void lua_open_esposito_libs(lua_State *L) {
    luaL_newlib(L, esposito_funcs);
    lua_setglobal(L, "esposito");
}
