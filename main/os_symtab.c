#include "os_symtab.h"
#include "app_heap.h"
#include "app_config.h"
#include "os_core.h"
#include "hardware.h"
#include "checkpoint.h"
#include "app_launcher.h"
#include "terminal_mode.h"
#include "text_mode.h"
#include "fonts.h"
#include "wifi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static const os_symtab_entry_t symtab[] = {
    {"display_clear",           display_clear},
    {"display_draw_text",       display_draw_text},
    {"display_draw_pixel",      display_draw_pixel},
    {"display_fill_rect",       display_fill_rect},
    {"display_draw_char_at",    display_draw_char_at},
    {"keyboard_read_event",     keyboard_read_event},
    {"checkpoint_save_string",  checkpoint_save_string},
    {"checkpoint_load_string",  checkpoint_load_string},
    {"checkpoint_save_int",     checkpoint_save_int},
    {"checkpoint_load_int",     checkpoint_load_int},
    {"checkpoint_save",         checkpoint_save},
    {"checkpoint_open",         checkpoint_open},
    {"checkpoint_close",        checkpoint_close},
    {"os_load_app",             os_load_app},
    {"os_open_app_with_file",   os_open_app_with_file},
    {"os_consume_startup_file", os_consume_startup_file},
    {"os_get_time_status",      os_get_time_status},
    {"os_time_is_synchronized", os_time_is_synchronized},
    {"os_time_last_sync",       os_time_last_sync},
    {"os_get_current_app",      os_get_current_app},
    {"app_launcher_start",      app_launcher_start},
    {"text_mode_init",          text_mode_init},
    {"text_mode_init_ex",       text_mode_init_ex},
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
    {"text_mode_save",          text_mode_save},
    {"text_mode_restore",       text_mode_restore},
    {"text_mode_flush",         text_mode_flush},
    {"printf",                  printf},
    {"puts",                    puts},
    {"sprintf",                 sprintf},
    {"snprintf",                snprintf},
    {"memset",                  memset},
    {"memcpy",                  memcpy},
    {"memmove",                 memmove},
    {"strlen",                  strlen},
    {"strcmp",                  strcmp},
    {"strncmp",                 strncmp},
    {"strcpy",                  strcpy},
    {"strncpy",                 strncpy},
    {"strcat",                  strcat},
    {"strchr",                  strchr},
    {"strrchr",                 strrchr},
    {"strstr",                  strstr},
    {"malloc",                  app_malloc},
    {"calloc",                  app_calloc},
    {"realloc",                 app_realloc},
    {"free",                    app_free},
    {"atoi",                    atoi},
    {"atol",                    atol},
    {"abs",                     abs},
    {"opendir",                 opendir},
    {"readdir",                 readdir},
    {"closedir",                closedir},
    {"stat",                    stat},
    {"mkdir",                   mkdir},
    {"os_log",                  os_log},
    {"terminal_mode_default",   terminal_mode_default},
    {"terminal_mode_init",      terminal_mode_init},
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
    {"font_table",              (void*)font_table},
    {"font_lookup_by_name",     font_lookup_by_name},
    {"wifi_save_config",        wifi_save_config},
    {"fopen",                   fopen},
    {"fread",                   fread},
    {"fwrite",                  fwrite},
    {"fclose",                  fclose},
    {"fseek",                   fseek},
    {"ftell",                   ftell},
    {"fgets",                   fgets},
    {"config_open_read",        config_open_read},
    {"config_open_write",       config_open_write},
    {"config_exists",           config_exists},
    {"config_delete",           config_delete},
    {"config_read_all_alloc",   config_read_all_alloc},
    {"config_free",             config_free},
    {"config_get_int",          config_get_int},
    {"config_get_float",        config_get_float},
    {"config_get_bool",         config_get_bool},
    {"config_get_string",       config_get_string},
    {"config_set_int",          config_set_int},
    {"config_set_float",        config_set_float},
    {"config_set_bool",         config_set_bool},
    {"config_set_string",       config_set_string},
    {"config_bind_app",         config_bind_app},
    {"config_unbind_app",       config_unbind_app},
    {"os_unload_app",           os_unload_app},
    {"fputc",                   fputc},
    {"time",                    time},
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
