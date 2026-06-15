#include "os_core.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void os_post_event(event_t *event) {
    (void)event;
}

void os_log(const char *tag, const char *fmt, ...) {
    va_list ap;
    fprintf(stdout, "[%s] ", tag);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

bool os_load_app(const char *app_name) {
    (void)app_name;
    return false;
}

void os_exit(void) {
    exit(0);
}

bool os_open_app_with_file(const char *app_name, const char *file_path) {
    (void)app_name;
    (void)file_path;
    return false;
}

size_t os_consume_startup_file(char *out, size_t out_size) {
    if (out && out_size > 0) out[0] = '\0';
    return 0;
}

bool os_get_time_status(os_time_status_t *status) {
    if (!status) return false;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    status->unix_time = (int64_t)now;
    status->last_sync_time = (int64_t)now;
    status->year = tm->tm_year + 1900;
    status->month = tm->tm_mon + 1;
    status->day = tm->tm_mday;
    status->hour = tm->tm_hour;
    status->minute = tm->tm_min;
    status->second = tm->tm_sec;
    status->weekday = tm->tm_wday;
    status->synchronized = true;
    return true;
}

bool os_time_is_synchronized(void) { return true; }
int64_t os_time_last_sync(void) { return (int64_t)time(NULL); }

int os_http_get(const char *url, char *out, size_t out_size, int timeout_ms) {
    (void)url;
    (void)out;
    (void)out_size;
    (void)timeout_ms;
    return -1; // Not implemented
}

int os_http_post(const char *url, const char *post_data, const char *extra_headers[],
                 const char *ca_pem, char *out, size_t out_size, int timeout_ms) {
    (void)url; (void)post_data; (void)extra_headers; (void)ca_pem;
    (void)out; (void)out_size; (void)timeout_ms;
    return -1;
}

int os_http_download(const char *url, const char *path, void (*progress)(int percent, const char *status)) {
    (void)url; (void)path; (void)progress;
    return -1;
}

bool os_download_via_os(const char *url, const char *path, size_t expected_size) {
    (void)url; (void)path; (void)expected_size;
    return false;
}

size_t os_settings_get_string(const char *key_path, const char *default_value, char *out, size_t out_size) {
    (void)key_path;
    if (out && out_size > 0) {
        snprintf(out, out_size, "%s", default_value ? default_value : "");
    }
    return 0;
}

bool os_settings_set_string(const char *key_path, const char *value) {
    (void)key_path;
    (void)value;
    return true;
}

int os_settings_get_int(const char *key_path, int default_value) {
    (void)key_path;
    return default_value;
}

bool os_settings_set_int(const char *key_path, int value) {
    (void)key_path;
    (void)value;
    return true;
}

bool os_settings_get_bool(const char *key_path, bool default_value) {
    (void)key_path;
    return default_value;
}

bool os_settings_set_bool(const char *key_path, bool value) {
    (void)key_path;
    (void)value;
    return true;
}

bool os_has_capability(const char *cap) {
    if (strcmp(cap, "keyboard") == 0) return true;
    if (strcmp(cap, "touch") == 0) return false;
    if (strcmp(cap, "wifi") == 0) return false;
    return false;
}
