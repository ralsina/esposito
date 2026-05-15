#ifndef APP_MANIFEST_H
#define APP_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MANIFEST_NAME_MAX 64
#define APP_MANIFEST_EXT_MAX 128

// Per-app manifest describing how the OS interacts with an SD-card app.
// Loaded from /sdcard/apps/<appname>/manifest.cfg at runtime.
// All fields have sensible defaults when the file is absent.
typedef struct {
    char display_name[APP_MANIFEST_NAME_MAX]; // Human-readable name shown in launcher
    char extensions[APP_MANIFEST_EXT_MAX];    // Comma-separated list, e.g. "txt,md,c"
    bool show_in_launcher;                    // Whether to list in app launcher
} app_sd_manifest_t;

// Read manifest for app_name (its directory name on the SD card).
// Always fills defaults; returns true if a manifest.cfg was found and parsed.
bool app_manifest_read(const char *app_name, app_sd_manifest_t *out);

// Convenience: get just the display name for an app into out[0..out_size-1].
char *app_manifest_get_display_name(const char *app_name, char *out, size_t out_size);

// Find all apps (with program.elf) whose manifest declares the given extension.
// ext should NOT include a leading dot (e.g. pass "jpg", not ".jpg").
// app_names_out receives the directory names (not display names) of matching apps.
// Returns count of matching apps found.
int app_manifest_find_apps_for_ext(const char *ext, char (*app_names_out)[64], int max_apps);

#ifdef __cplusplus
}
#endif

#endif // APP_MANIFEST_H
