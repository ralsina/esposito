#include "os_core.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <SDL2/SDL.h>
#include <sys/stat.h>

// Set by os_load_app(); the main loop checks this to checkpoint and exit
// the current app, since the emulator can only run one app at a time.
static bool g_app_switch_pending = false;

#define SETTINGS_DIR "/tmp/esposito_settings"

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
    os_log("os", "Load requested: %s (exiting current app)", app_name ? app_name : "(null)");
    g_app_switch_pending = true;
    return true;
}

bool os_app_switch_pending(void) {
    return g_app_switch_pending;
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

// Settings persistence: one file per key under SETTINGS_DIR.
// Slashes in key paths are flattened to '_' so "wifi/ssid" -> "wifi_ssid".
static void settings_path(const char *key_path, char *buf, size_t buf_size) {
    mkdir(SETTINGS_DIR, 0755);
    char safe[128];
    size_t i = 0;
    if (key_path) {
        for (; key_path[i] && i < sizeof(safe) - 1; i++)
            safe[i] = (key_path[i] == '/') ? '_' : key_path[i];
    }
    safe[i] = '\0';
    snprintf(buf, buf_size, "%s/%s", SETTINGS_DIR, safe);
}

size_t os_settings_get_string(const char *key_path, const char *default_value, char *out, size_t out_size) {
    char path[256];
    settings_path(key_path, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) {
        if (out && out_size > 0)
            snprintf(out, out_size, "%s", default_value ? default_value : "");
        return 0;
    }
    size_t n = 0;
    if (out && out_size > 0 && fgets(out, (int)out_size, f)) {
        n = strlen(out);
        if (n > 0 && out[n - 1] == '\n') { out[n - 1] = '\0'; n--; }
    }
    fclose(f);
    return n;
}

bool os_settings_set_string(const char *key_path, const char *value) {
    char path[256];
    settings_path(key_path, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%s\n", value ? value : "");
    fclose(f);
    return true;
}

int os_settings_get_int(const char *key_path, int default_value) {
    char path[256];
    settings_path(key_path, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return default_value;
    int val;
    if (fscanf(f, "%d", &val) != 1) val = default_value;
    fclose(f);
    return val;
}

bool os_settings_set_int(const char *key_path, int value) {
    char path[256];
    settings_path(key_path, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%d", value);
    fclose(f);
    return true;
}

bool os_settings_get_bool(const char *key_path, bool default_value) {
    char path[256];
    settings_path(key_path, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return default_value;
    int val;
    if (fscanf(f, "%d", &val) != 1) val = default_value ? 1 : 0;
    fclose(f);
    return val != 0;
}

bool os_settings_set_bool(const char *key_path, bool value) {
    char path[256];
    settings_path(key_path, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%d", value ? 1 : 0);
    fclose(f);
    return true;
}

bool os_has_capability(const char *cap) {
    if (strcmp(cap, "keyboard") == 0) return true;
    if (strcmp(cap, "touch") == 0) return true;
    if (strcmp(cap, "wifi") == 0) return false;
    return false;
}

// --- Task API ---
os_task_handle_t *os_task_create(os_task_func_t task_func, const char *name, int stack_size, int priority, int core_id) {
    (void)name; (void)stack_size; (void)priority; (void)core_id;
    if (!task_func) return NULL;

    os_task_handle_t *handle = malloc(sizeof(os_task_handle_t));
    if (!handle) return NULL;

    pthread_t *thread = malloc(sizeof(pthread_t));
    if (!thread) { free(handle); return NULL; }

    if (pthread_create(thread, NULL, (void *(*)(void *))(void *)task_func, NULL) != 0) {
        free(thread);
        free(handle);
        return NULL;
    }

    handle->handle = thread;
    return handle;
}

void os_task_delete(os_task_handle_t *task) {
    if (!task || !task->handle) return;
    pthread_t *thread = (pthread_t *)task->handle;
    pthread_cancel(*thread);
    pthread_join(*thread, NULL);
    free(thread);
    free(task);
}

// --- Semaphore API ---
os_semaphore_handle_t *os_semaphore_create(void) {
    os_semaphore_handle_t *handle = malloc(sizeof(os_semaphore_handle_t));
    if (!handle) return NULL;

    SDL_sem *sem = SDL_CreateSemaphore(0);
    if (!sem) { free(handle); return NULL; }

    handle->handle = sem;
    return handle;
}

void os_semaphore_give(os_semaphore_handle_t *sem) {
    if (!sem || !sem->handle) return;
    SDL_SemPost((SDL_sem *)sem->handle);
}

bool os_semaphore_take(os_semaphore_handle_t *sem, int timeout_ms) {
    if (!sem || !sem->handle) return false;
    SDL_sem *s = (SDL_sem *)sem->handle;

    if (timeout_ms < 0) return SDL_SemWait(s) == 0;
    if (timeout_ms == 0) return SDL_SemTryWait(s) == 0;
    return SDL_SemWaitTimeout(s, timeout_ms) == 0;
}

void os_semaphore_delete(os_semaphore_handle_t *sem) {
    if (!sem || !sem->handle) return;
    SDL_DestroySemaphore((SDL_sem *)sem->handle);
    free(sem);
}

bool os_set_cpu_freq_mhz(int freq_mhz) {
    (void)freq_mhz;
    return true;
}

int64_t esp_timer_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}
