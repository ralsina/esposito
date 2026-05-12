// App Launcher Header

#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

#include <stdbool.h>
#include "os_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the app launcher
void app_launcher_start(void);

// Check if launcher is currently active
bool app_launcher_is_active(void);

// Handle events in launcher mode
void app_launcher_handle_event(event_t *event);

#ifdef __cplusplus
}
#endif

#endif // APP_LAUNCHER_H