#ifndef BOOT_H
#define BOOT_H

#include <stdbool.h>

typedef enum {
    BOOT_STAGE_POWER_ON = 0,
    BOOT_STAGE_HARDWARE_INIT,
    BOOT_STAGE_DISPLAY_INIT,
    BOOT_STAGE_FILESYSTEM_INIT,
    BOOT_STAGE_KEYBOARD_INIT,
    BOOT_STAGE_APP_LOADER_INIT,
    BOOT_STAGE_LOAD_DEFAULT_APP,
    BOOT_STAGE_COMPLETE,
    BOOT_STAGE_FAILED
} boot_stage_t;

typedef struct {
    boot_stage_t stage;
    const char *stage_name;
    bool success;
    const char *error_message;
} boot_status_t;

// Global boot status (extern for main.c access)
extern boot_status_t boot_status;

// Boot functions
#ifdef __cplusplus
extern "C" {
#endif

void boot_sequence(void);
bool boot_display_init(void);
void boot_display_progress(boot_stage_t stage, bool success, const char *message);
void boot_display_splash(void);

#ifdef __cplusplus
}
#endif

#endif // BOOT_H
