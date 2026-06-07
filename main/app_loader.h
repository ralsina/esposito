#ifndef APP_LOADER_H
#define APP_LOADER_H

#include <stdbool.h>

#define APP_LOADER_MAX_APPS 32
#define APP_LOADER_MAX_NAME_LEN 256

// App loader initialization
#ifdef __cplusplus
extern "C" {
#endif

bool app_loader_init(void);

// Scan for available apps
int app_loader_scan(char (*app_names)[APP_LOADER_MAX_NAME_LEN], int max_apps);

// Load an app by name
bool app_loader_load(const char *app_name);

// Get number of available apps
int app_loader_get_count(void);

#ifdef __cplusplus
}
#endif

#endif // APP_LOADER_H
