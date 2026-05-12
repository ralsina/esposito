#include "os_symtab.h"
#include "os_core.h"
#include "hardware.h"
#include "checkpoint.h"
#include "text_mode.h"
#include "wifi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    {"os_get_current_app",      os_get_current_app},
    {"text_mode_init",          text_mode_init},
    {"text_mode_clear",         text_mode_clear},
    {"text_mode_print_at",      text_mode_print_at},
    {"text_mode_print_at_color",text_mode_print_at_color},
    {"text_mode_printf_at",     text_mode_printf_at},
    {"text_mode_printf_at_color",text_mode_printf_at_color},
    {"text_mode_print_at_attr", text_mode_print_at_attr},
    {"text_mode_printf_at_attr",text_mode_printf_at_attr},
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
    {"malloc",                  malloc},
    {"calloc",                  calloc},
    {"realloc",                 realloc},
    {"free",                    free},
    {"atoi",                    atoi},
    {"atol",                    atol},
    {"abs",                     abs},
    {"os_log",                  os_log},
    {"serial_init",             serial_init},
    {"serial_write",            serial_write},
    {"wifi_init",               wifi_init},
    {"wifi_is_connected",       wifi_is_connected},
    {"wifi_get_ip",             wifi_get_ip},
    {"wifi_scan",               wifi_scan},
    {"wifi_scan_get_ssid",      wifi_scan_get_ssid},
    {"wifi_scan_get_rssi",      wifi_scan_get_rssi},
    {"wifi_connect",            wifi_connect},
    {"wifi_disconnect",         wifi_disconnect},
    {"wifi_save_config",        wifi_save_config},
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
