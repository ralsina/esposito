#ifndef OS_CORE_H
#define OS_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Event types
typedef enum {
    EVENT_TIMER = 1 << 0,
    EVENT_TOUCH = 1 << 1,
    EVENT_SERIAL = 1 << 2,
    EVENT_KEYBOARD = 1 << 3,
    EVENT_KEY_COMBO = 1 << 4,
    EVENT_TOUCH_CONTINUOUS = 1 << 5,
} event_type_t;

// Keyboard modifier flags
typedef enum {
    MODIFIER_SHIFT = 1 << 0,
    MODIFIER_CTRL  = 1 << 1,
    MODIFIER_ALT   = 1 << 2,
    MODIFIER_FN    = 1 << 3,
    MODIFIER_FN2   = 1 << 4,
} key_modifier_t;

// Event structure
typedef struct {
    event_type_t type;
    union {
        struct {
            uint16_t x;
            uint16_t y;
            bool pressed;
        } touch;
        struct {
            char key;
            bool pressed;
            uint8_t modifiers;  // Modifier key state (Ctrl, Alt, Shift, etc.)
            uint8_t raw_key_code;  // Raw key code from keyboard (for non-ASCII keys)
        } keyboard;
        struct {
            char data[256];
            size_t len;
        } serial;
        struct {
            uint32_t combo_id;
        } key_combo;
    };
} event_t;

// App context
typedef struct app_context app_context_t;

// App interface functions
typedef void (*app_init_fn)(app_context_t *ctx);
typedef void (*app_checkpoint_fn)(app_context_t *ctx);
typedef void (*app_close_fn)(app_context_t *ctx);
typedef void (*app_event_fn)(app_context_t *ctx, event_t *event);

// App structure
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
};

// App manifest structure for built-in apps
typedef struct {
    const char *name;
    app_init_fn init;
    app_event_fn event_fn;
    app_close_fn close;
    app_checkpoint_fn checkpoint;
    uint32_t subscriptions;
} app_manifest_t;

typedef struct {
    int64_t unix_time;
    int64_t last_sync_time;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
    bool synchronized;
} os_time_status_t;

// OS core functions
#ifdef __cplusplus
extern "C" {
#endif

void os_log(const char *tag, const char *fmt, ...);
bool os_init_filesystem(void);
void os_event_loop(void);
bool os_load_app(const char *app_name);
bool os_open_app_with_file(const char *app_name, const char *file_path);
size_t os_consume_startup_file(char *out, size_t out_size);
bool os_get_time_status(os_time_status_t *status);
bool os_time_is_synchronized(void);
int64_t os_time_last_sync(void);
int os_http_get(const char *url, char *out, size_t out_size, int timeout_ms);
size_t os_settings_get_string(const char *key_path,
                              const char *default_value,
                              char *out,
                              size_t out_size);
bool os_settings_set_string(const char *key_path, const char *value);
int os_settings_get_int(const char *key_path, int default_value);
bool os_settings_set_int(const char *key_path, int value);
bool os_settings_get_bool(const char *key_path, bool default_value);
bool os_settings_set_bool(const char *key_path, bool value);
void os_unload_app(void);
app_context_t *os_get_current_app(void);
void os_set_current_app(app_context_t *app);

// Checkpoint API for apps
void checkpoint_save_string(const char *key, const char *value);
const char *checkpoint_load_string(const char *key);
void checkpoint_save_int(const char *key, int value);
int checkpoint_load_int(const char *key);

#ifdef __cplusplus
}
#endif

#endif // OS_CORE_H
