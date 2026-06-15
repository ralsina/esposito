#ifndef OS_CORE_H
#define OS_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    EVENT_TIMER = 1 << 0,
    EVENT_TOUCH = 1 << 1,
    EVENT_SERIAL = 1 << 2,
    EVENT_KEYBOARD = 1 << 3,
    EVENT_KEY_COMBO = 1 << 4,
    EVENT_TOUCH_CONTINUOUS = 1 << 5,
} event_type_t;

typedef enum {
    MODIFIER_SHIFT = 1 << 0,
    MODIFIER_CTRL  = 1 << 1,
    MODIFIER_ALT   = 1 << 2,
    MODIFIER_FN    = 1 << 3,
    MODIFIER_FN2   = 1 << 4,
} key_modifier_t;

typedef struct {
    event_type_t type;
    union {
        struct { uint16_t x, y; bool pressed; } touch;
        struct {
            char key;
            bool pressed;
            uint8_t modifiers;
            uint8_t raw_key_code;
        } keyboard;
        struct { char data[256]; size_t len; } serial;
        struct { uint32_t combo_id; } key_combo;
    };
} event_t;

typedef struct app_context app_context_t;

typedef void (*app_init_fn)(app_context_t *ctx);
typedef void (*app_checkpoint_fn)(app_context_t *ctx);
typedef void (*app_close_fn)(app_context_t *ctx);
typedef void (*app_event_fn)(app_context_t *ctx, event_t *event);

struct app_context {
    char name[64];
    void *handle;
    app_init_fn init;
    app_checkpoint_fn checkpoint;
    app_close_fn close;
    app_event_fn event_fn;
    uint32_t subscriptions;
    uint32_t timer_interval_ms;
    void *user_data;
    int requested_cpu_freq_mhz;
};

typedef struct {
    int64_t unix_time;
    int64_t last_sync_time;
    int year, month, day;
    int hour, minute, second;
    int weekday;
    bool synchronized;
} os_time_status_t;

#ifdef __cplusplus
extern "C" {
#endif

void os_post_event(event_t *event);
void os_log(const char *tag, const char *fmt, ...);
bool os_load_app(const char *app_name);
void os_exit(void);
bool os_open_app_with_file(const char *app_name, const char *file_path);
size_t os_consume_startup_file(char *out, size_t out_size);
bool os_get_time_status(os_time_status_t *status);
bool os_time_is_synchronized(void);
int64_t os_time_last_sync(void);
int os_http_get(const char *url, char *out, size_t out_size, int timeout_ms);
int os_http_post(const char *url, const char *post_data, const char *extra_headers[],
                 const char *ca_pem, char *out, size_t out_size, int timeout_ms);
int os_http_download(const char *url, const char *path, void (*progress)(int percent, const char *status));
bool os_download_via_os(const char *url, const char *path, size_t expected_size);

// Settings (stubs)
size_t os_settings_get_string(const char *key_path, const char *default_value, char *out, size_t out_size);
bool os_settings_set_string(const char *key_path, const char *value);
int os_settings_get_int(const char *key_path, int default_value);
bool os_settings_set_int(const char *key_path, int value);
bool os_settings_get_bool(const char *key_path, bool default_value);
bool os_settings_set_bool(const char *key_path, bool value);
bool os_has_capability(const char *cap);

// Task and synchronization API
typedef void (*os_task_func_t)(void *parameter);

typedef struct {
    void *handle;
} os_task_handle_t;

typedef struct {
    void *handle;
} os_semaphore_handle_t;

os_task_handle_t *os_task_create(os_task_func_t task_func, const char *name, int stack_size, int priority, int core_id);
void os_task_delete(os_task_handle_t *task);
os_semaphore_handle_t *os_semaphore_create(void);
void os_semaphore_give(os_semaphore_handle_t *sem);
bool os_semaphore_take(os_semaphore_handle_t *sem, int timeout_ms);
void os_semaphore_delete(os_semaphore_handle_t *sem);

bool os_set_cpu_freq_mhz(int freq_mhz);
int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif

#endif
