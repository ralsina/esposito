#include "os_symtab.h"
#include "app_heap.h"
#include "app_config.h"
#include "app_manifest.h"
#include "app_loader.h"
#include "os_core.h"
#include "hardware.h"
#include "app_launcher.h"
#include "terminal_mode.h"
#include "text_mode.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "graphics_mode.h"
#include "fonts.h"
#include "wifi.h"
#include "ota_update.h"
#include "ble_keyboard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <setjmp.h>
#include "esp_timer.h"

// strdup reimplementation that uses app_malloc so it matches app_free
char *strdup_impl(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = (char *)app_malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

static const os_symtab_entry_t symtab[] = {
    {"display_clear",           display_clear},
    {"display_draw_text",       display_draw_text},
    {"display_draw_text_bg",    display_draw_text_bg},
    {"display_draw_pixel",      display_draw_pixel},
    {"display_fill_rect",       display_fill_rect},
    {"display_draw_char_at",    display_draw_char_at},
    {"display_draw_unicode_at", display_draw_unicode_at},
    {"display_draw_unicode_with_font", display_draw_unicode_with_font},
    {"display_measure_scaled_text", display_measure_scaled_text},
    {"display_draw_scaled_text_bg", display_draw_scaled_text_bg},
    {"display_get_jpg_size",    display_get_jpg_size},
    {"display_draw_jpg_fit",    display_draw_jpg_fit},
    {"display_get_width",       display_get_width},
    {"display_get_height",      display_get_height},
    {"display_set_rotation",         display_set_rotation},
    {"display_get_rotation",         display_get_rotation},
    {"display_apply_saved_rotation", display_apply_saved_rotation},
    {"display_apply_saved_backlight", display_apply_saved_backlight},
    {"display_set_backlight",        display_set_backlight},
    {"display_start_write",          display_start_write},
    {"display_end_write",            display_end_write},
    {"display_set_window",           display_set_window},
    {"display_push_pixels",          display_push_pixels},
    {"display_create_sprite",        display_create_sprite},
    {"sprite_set_palette_color",     sprite_set_palette_color},
    {"sprite_draw_pixel",            sprite_draw_pixel},
    {"sprite_write_row",             sprite_write_row},
    {"sprite_push",                  sprite_push},
    {"sprite_push_rotated_zoom",     sprite_push_rotated_zoom},
    {"sprite_set_pivot",             sprite_set_pivot},
    {"sprite_destroy",               sprite_destroy},
    {"sprite_set_active",            sprite_set_active},
    {"sprite_get_active",            sprite_get_active},
    {"flash_rom_load",               flash_rom_load},
    {"flash_rom_unload",             flash_rom_unload},
    {"led_set_rgb",                  led_set_rgb},
    {"keyboard_read_event",          keyboard_read_event},
    {"keyboard_set_backlight",       keyboard_set_backlight},
    {"keyboard_get_backlight",       keyboard_get_backlight},
    {"os_get_board_info",            os_get_board_info},
    {"os_load_app",                  os_load_app},
    {"os_exit",                      os_exit},
    {"os_process_one_event_iteration", os_process_one_event_iteration},
    {"os_open_app_with_file",   os_open_app_with_file},
    {"os_consume_startup_file", os_consume_startup_file},
    {"os_get_time_status",      os_get_time_status},
    {"os_time_is_synchronized", os_time_is_synchronized},
    {"os_time_last_sync",       os_time_last_sync},
    {"os_http_get",             os_http_get},
    {"os_http_download",        os_http_download},
    {"os_download_via_os",      os_download_via_os},
    {"os_http_post",            os_http_post},
    {"os_settings_get_string",  os_settings_get_string},
    {"os_settings_set_string",  os_settings_set_string},
    {"os_settings_get_int",     os_settings_get_int},
    {"os_settings_set_int",     os_settings_set_int},
    {"os_settings_get_bool",    os_settings_get_bool},
    {"os_settings_set_bool",    os_settings_set_bool},
    {"os_has_capability",       os_has_capability},
    {"os_get_chip_arch",        os_get_chip_arch},
    {"os_get_current_app",      os_get_current_app},
    // BLE HID Host
    {"ble_keyboard_is_available",    ble_keyboard_is_available},
    {"ble_keyboard_is_initializing", ble_keyboard_is_initializing},
    {"ble_keyboard_start_scan",      ble_keyboard_start_scan},
    {"ble_keyboard_get_scan_count",  ble_keyboard_get_scan_count},
    {"ble_keyboard_get_scan_name",   ble_keyboard_get_scan_name},
    {"ble_keyboard_get_scan_rssi",   ble_keyboard_get_scan_rssi},
    {"ble_keyboard_connect",         ble_keyboard_connect},
    {"ble_keyboard_disconnect",      ble_keyboard_disconnect},
    {"ble_keyboard_is_connected",    ble_keyboard_is_connected},
    {"ble_keyboard_get_connected_name", ble_keyboard_get_connected_name},
    {"ble_keyboard_save_device",     ble_keyboard_save_device},
    {"ble_keyboard_reconnect",       ble_keyboard_reconnect},
    {"appcfg_get_int",          appcfg_get_int},
    {"appcfg_get_string",       appcfg_get_string},
    {"config_delete",           config_delete},
    {"app_launcher_start",      app_launcher_start},
    {"app_launcher_is_active",  app_launcher_is_active},
    {"app_loader_scan",         app_loader_scan},
    {"app_loader_get_count",    app_loader_get_count},
    {"text_mode_init",          text_mode_init},
    {"text_mode_init_ex",       text_mode_init_ex},
    {"text_mode_set_font",      text_mode_set_font},
    {"text_mode_get_cols",      text_mode_get_cols},
    {"text_mode_get_rows",      text_mode_get_rows},
    {"text_mode_get_char_width", text_mode_get_char_width},
    {"text_mode_get_char_height",text_mode_get_char_height},
    {"text_mode_get_font",      text_mode_get_font},
    {"text_mode_clear",         text_mode_clear},
    {"text_mode_print_at",      text_mode_print_at},
    {"text_mode_print_at_color",text_mode_print_at_color},
    {"text_mode_printf_at",     text_mode_printf_at},
    {"text_mode_printf_at_color",text_mode_printf_at_color},
    {"text_mode_print_at_attr", text_mode_print_at_attr},
    {"text_mode_printf_at_attr",text_mode_printf_at_attr},
    {"text_mode_print_at_attr_bg", text_mode_print_at_attr_bg},
    {"text_mode_printf_at_attr_bg",text_mode_printf_at_attr_bg},
    {"text_mode_get_cursor",    text_mode_get_cursor},
    {"text_mode_set_cursor",    text_mode_set_cursor},
    {"text_mode_flush",         text_mode_flush},
    {"text_mode_set_font",      text_mode_set_font},
    {"text_mode_apply_configured_font", text_mode_apply_configured_font},
    {"text_mode_save_snapshot", text_mode_save_snapshot},
    {"text_mode_restore_snapshot", text_mode_restore_snapshot},
    {"text_mode_free_snapshot", text_mode_free_snapshot},
    {"text_mode_pixel_to_cell", text_mode_pixel_to_cell},
    {"text_mode_cell_to_pixel", text_mode_cell_to_pixel},
    {"text_mode_set_palette",   text_mode_set_palette},
    {"text_mode_apply_configured_palette", text_mode_apply_configured_palette},
    {"graphics_mode_init",        graphics_mode_init},
    {"graphics_mode_deinit",      graphics_mode_deinit},
    {"graphics_mode_is_active",   graphics_mode_is_active},
    {"graphics_set_palette",      graphics_set_palette},
    {"graphics_clear",            graphics_clear},
    {"graphics_draw_pixel",       graphics_draw_pixel},
    {"graphics_draw_line",        graphics_draw_line},
    {"graphics_fill_rect",        graphics_fill_rect},
    {"graphics_draw_rect",        graphics_draw_rect},
    {"graphics_draw_string",      graphics_draw_string},
    {"graphics_flush",            graphics_flush},
    {"graphics_mode_get_buffer",  graphics_mode_get_buffer},
    {"graphics_mode_get_buffer_size", graphics_mode_get_buffer_size},
    {"graphics_blit_scaled",       graphics_blit_scaled},
    {"printf",                  printf},
    {"puts",                    puts},
    {"sprintf",                 sprintf},
    {"snprintf",                snprintf},
    {"memset",                  memset},
    {"memcmp",                  memcmp},
    {"memcpy",                  memcpy},
    {"memmove",                 memmove},
    {"strlen",                  strlen},
    {"strcmp",                  strcmp},
    {"strncmp",                 strncmp},
    {"strcpy",                  strcpy},
    {"strncpy",                 strncpy},
    {"strcat",                  strcat},
    {"strdup",                  strdup_impl},
    {"strchr",                  strchr},
    {"strrchr",                 strrchr},
    {"strstr",                  strstr},
    {"malloc",                  app_malloc},
    {"calloc",                  app_calloc},
    {"realloc",                 app_realloc},
    {"free",                    app_free},
    {"atoi",                    atoi},
    {"atol",                    atol},
    {"atof",                    atof},
    {"abs",                     abs},
    {"qsort",                   qsort},
    {"opendir",                 opendir},
    {"readdir",                 readdir},
    {"closedir",                closedir},
    {"stat",                    stat},
    {"mkdir",                   mkdir},
    {"os_log",                  os_log},
    {"terminal_mode_struct_size", terminal_mode_struct_size},
    {"terminal_mode_clear_active", terminal_mode_clear_active},
    {"terminal_mode_init",      terminal_mode_init},
    {"terminal_mode_init_ex",   terminal_mode_init_ex},
    {"terminal_mode_reset",     terminal_mode_reset},
    {"terminal_mode_set_write_callback", terminal_mode_set_write_callback},
    {"terminal_mode_set_title_callback", terminal_mode_set_title_callback},
    {"terminal_mode_process_bytes", terminal_mode_process_bytes},
    {"terminal_mode_handle_key", terminal_mode_handle_key},
    {"terminal_mode_set_status", terminal_mode_set_status},
    {"terminal_mode_render",    terminal_mode_render},
    {"terminal_mode_cols",      terminal_mode_cols},
    {"terminal_mode_rows",      terminal_mode_rows},
    {"terminal_mode_normalize_key", terminal_mode_normalize_key},
    {"serial_init",             serial_init},
    {"serial_deinit",           serial_deinit},
    {"serial_read",             serial_read},
    {"serial_write",            serial_write},
    {"serial_log_output_set_enabled", serial_log_output_set_enabled},
    {"serial_log_output_is_enabled", serial_log_output_is_enabled},
    {"wifi_init",               wifi_init},
    {"wifi_is_connected",       wifi_is_connected},
    {"wifi_get_ip",             wifi_get_ip},
    {"wifi_scan",               wifi_scan},
    {"wifi_scan_get_ssid",      wifi_scan_get_ssid},
    {"wifi_scan_get_rssi",      wifi_scan_get_rssi},
    {"wifi_connect",            wifi_connect},
    {"wifi_disconnect",         wifi_disconnect},
    {"font_table",              (void*)&font_table},
    {"font_count",              (void*)&font_count},
    {"font_lookup_by_name",     font_lookup_by_name},
    {"wifi_save_config",        wifi_save_config},
    {"fopen",                   fopen},
    {"fread",                   fread},
    {"fwrite",                  fwrite},
    {"fclose",                  fclose},
    {"fseek",                   fseek},
    {"ftell",                   ftell},
    {"fgets",                   fgets},
    {"fflush",                  fflush},
    {"rename",                  rename},
    {"remove",                  remove},
    {"config_open_read",        config_open_read},
    {"config_open_write",       config_open_write},
    {"config_exists",           config_exists},
    {"config_delete",           config_delete},
    {"config_read_all_alloc",   config_read_all_alloc},
    {"config_free",             appcfg_free},
    {"config_get_int",          appcfg_get_int},
    {"config_get_float",        appcfg_get_float},
    {"config_get_bool",         appcfg_get_bool},
    {"config_get_string",       appcfg_get_string},
    {"config_set_int",          appcfg_set_int},
    {"config_set_float",        appcfg_set_float},
    {"config_set_bool",         appcfg_set_bool},
    {"config_set_string",       appcfg_set_string},
    {"config_bind_app",         config_bind_app},
    {"config_unbind_app",       config_unbind_app},
    {"os_unload_app",           os_unload_app},
    {"fputc",                   fputc},
    {"keyboard_is_available",   keyboard_is_available},
    {"esp_timer_get_time",           esp_timer_get_time},
    {"time",                    time},
    {"app_manifest_read",              app_manifest_read},
    {"app_manifest_get_display_name",  app_manifest_get_display_name},
    {"app_manifest_find_apps_for_ext", app_manifest_find_apps_for_ext},
    {"app_manifest_write",              app_manifest_write},
    {"ota_firmware_version",    ota_firmware_version},
    {"ota_check_for_update",    ota_check_for_update},
    {"ota_apply_update",        ota_apply_update},

    // Standard C library functions
    {"rand",                    rand},
    {"srand",                   srand},

    // Floating point math functions (float versions for ESP32 FPU)
    {"sinf",                    sinf},
    {"cosf",                    cosf},
    {"tanf",                    tanf},
    {"asinf",                   asinf},
    {"acosf",                   acosf},
    {"atanf",                   atanf},
    {"atan2f",                  atan2f},
    {"sinhf",                   sinhf},
    {"coshf",                   coshf},
    {"tanhf",                   tanhf},
    {"expf",                    expf},
    {"logf",                    logf},
    {"log10f",                  log10f},
    {"powf",                    powf},
    {"sqrtf",                   sqrtf},
    {"ceilf",                   ceilf},
    {"floorf",                  floorf},
    {"fabsf",                   fabsf},
    {"fmodf",                   fmodf},
    {"modff",                   modff},
    {"frexpf",                  frexpf},
    {"ldexpf",                  ldexpf},

    // Double-precision versions (software emulation, use sparingly)
    {"sin",                     sin},
    {"cos",                     cos},
    {"tan",                     tan},
    {"asin",                    asin},
    {"acos",                    acos},
    {"atan",                    atan},
    {"atan2",                   atan2},
    {"sinh",                    sinh},
    {"cosh",                    cosh},
    {"tanh",                    tanh},
    {"exp",                     exp},
    {"log",                     log},
    {"log10",                   log10},
    {"pow",                     pow},
    {"sqrt",                    sqrt},
    {"ceil",                    ceil},
    {"floor",                   floor},
    {"fabs",                    fabs},
    {"fmod",                    fmod},
    {"modf",                    modf},
    {"frexp",                   frexp},
    {"ldexp",                   ldexp},

    // Floating point conversion
    {"strtof",                  strtof},
    {"strtod",                  strtod},
    {"strto",                   strtod},

    // Floating point utilities
    {"isnan",                   isnan},
    {"isinf",                   isinf},

    // Additional printf support for floats
    {"sscanf",                  sscanf},
    {"vsscanf",                 vsscanf},
    {"vsnprintf",               vsnprintf},

    // C library support for ctype functions
    {"_ctype_",                 (void*)_ctype_},

    // Task and synchronization API
    {"os_task_create",          (void*)os_task_create},
    {"os_task_delete",          (void*)os_task_delete},
    {"os_semaphore_create",     (void*)os_semaphore_create},
    {"os_semaphore_give",       (void*)os_semaphore_give},
    {"os_semaphore_take",       (void*)os_semaphore_take},
    {"os_semaphore_delete",     (void*)os_semaphore_delete},

    // CPU frequency control
    {"os_set_cpu_freq_mhz",     (void*)os_set_cpu_freq_mhz},

    // Lua needs these for error handling (pcall/xpcall via setjmp/longjmp)
    {"setjmp",                  setjmp},
    {"longjmp",                 longjmp},

    // Lua C API (embedded in firmware, app allocates from app heap via custom allocator)
    {"lua_newstate",            lua_newstate},
    {"lua_close",               lua_close},
    {"luaL_openlibs",           luaL_openlibs},
    {"luaL_loadstring",         luaL_loadstring},
    {"lua_pcallk",              lua_pcallk},
    {"lua_tolstring",           lua_tolstring},
    {"lua_settop",              lua_settop},
    {"lua_gettop",              lua_gettop},
    {"lua_pushinteger",         lua_pushinteger},
    {"lua_pushnumber",          lua_pushnumber},
    {"lua_pushstring",          lua_pushstring},
    {"lua_getglobal",           lua_getglobal},
    {"lua_setglobal",           lua_setglobal},
    {"lua_error",               lua_error},
    {"luaL_error",              luaL_error},
    {"lua_getfield",            lua_getfield},
    {"lua_setfield",            lua_setfield},
    {"lua_next",                lua_next},
    {"lua_pushnil",             lua_pushnil},
    {"lua_pushboolean",         lua_pushboolean},
    {"lua_toboolean",           lua_toboolean},
    {"lua_type",                lua_type},
    {"lua_typename",            lua_typename},
    {"lua_isinteger",           lua_isinteger},
    {"lua_isstring",            lua_isstring},
    {"lua_tonumberx",           lua_tonumberx},
    {"lua_tointegerx",          lua_tointegerx},
    {"lua_pushvalue",           lua_pushvalue},
    {"lua_pushlstring",         lua_pushlstring},
    {"lua_pushcclosure",        lua_pushcclosure},
    {"lua_createtable",         lua_createtable},
    {"lua_setmetatable",        lua_setmetatable},
    {"lua_getmetatable",        lua_getmetatable},
    {"lua_rawlen",              lua_rawlen},
    {"luaL_setfuncs",           luaL_setfuncs},
    {"luaL_requiref",           luaL_requiref},
    {"luaL_checkversion_",      luaL_checkversion_},
    {"luaL_checklstring",       luaL_checklstring},
    {"luaL_checkinteger",       luaL_checkinteger},
    {"luaL_optinteger",         luaL_optinteger},
    {"luaL_optlstring",         luaL_optlstring},
    {"lua_rawseti",             lua_rawseti},
    {"luaopen_base",            luaopen_base},
    {"luaopen_coroutine",       luaopen_coroutine},
    {"luaopen_table",           luaopen_table},
    {"luaopen_io",              luaopen_io},
    {"luaopen_os",              luaopen_os},
    {"luaopen_string",          luaopen_string},
    {"luaopen_utf8",            luaopen_utf8},
    {"luaopen_math",            luaopen_math},
    {"luaopen_debug",           luaopen_debug},
    {"luaopen_package",         luaopen_package},

    {NULL, NULL}
};

const os_symtab_entry_t *os_symtab_lookup(const char *name) {
    for (int i = 0; symtab[i].name != NULL; i++) {
        if (strcmp(symtab[i].name, name) == 0) {
            return &symtab[i];
        }
    }
    return NULL;
}

int os_symtab_count(void) {
    int count = 0;
    while (symtab[count].name != NULL) count++;
    return count;
}

const os_symtab_entry_t *os_symtab_get(int index) {
    return &symtab[index];
}
