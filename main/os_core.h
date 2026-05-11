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
} event_type_t;

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

// OS core functions
#ifdef __cplusplus
extern "C" {
#endif

bool os_init_filesystem(void);
void os_event_loop(void);
bool os_load_app(const char *app_name);
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
