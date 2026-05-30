#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

#include <stdbool.h>
#include "os_core.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_launcher_start(void);
bool app_launcher_is_active(void);

#ifdef __cplusplus
}
#endif

#endif
