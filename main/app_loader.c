#include "app_loader.h"
#include "app_heap.h"
#include "app_config.h"
#include "app_manifest.h"
#include "os_core.h"
#include "elf_loader.h"
#include "sd_card.h"
#include "hardware.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <dirent.h>
#include <stdio.h>

static const char *TAG = "app_loader";

static char s_cache_names[APP_LOADER_MAX_APPS][APP_LOADER_MAX_NAME_LEN];
static char s_cache_display_names[APP_LOADER_MAX_APPS][APP_LOADER_MAX_NAME_LEN];
static int s_cache_count = 0;
static bool s_cache_valid = false;

bool app_loader_init(void) {
    s_cache_valid = false;
    s_cache_count = 0;
    ESP_LOGI(TAG, "App loader initialized");
    return true;
}

void app_loader_invalidate_cache(void) {
    s_cache_valid = false;
    s_cache_count = 0;
    ESP_LOGI(TAG, "App list cache invalidated");
}

static int scan_sd_card(char (*app_names)[APP_LOADER_MAX_NAME_LEN], int max_apps) {
    int count = 0;

    if (sd_card_is_mounted()) {
        DIR *dir = opendir("/sdcard/apps");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL && count < max_apps) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

                char elf_path[512];
                snprintf(elf_path, sizeof(elf_path), "/sdcard/apps/%s/program.elf", entry->d_name);

                FILE *f = fopen(elf_path, "r");
                if (f) {
                    fclose(f);
                    // Skip apps that declare launcher=no in their manifest
                    app_sd_manifest_t manifest;
                    bool show = true;
                    if (app_manifest_read(entry->d_name, &manifest)) {
                        show = manifest.show_in_launcher;
                        snprintf(s_cache_display_names[count], APP_LOADER_MAX_NAME_LEN,
                                 "%s", manifest.display_name);
                    } else {
                        snprintf(s_cache_display_names[count], APP_LOADER_MAX_NAME_LEN,
                                 "%s", entry->d_name);
                    }
                    if (!show) continue;
                    snprintf(app_names[count], APP_LOADER_MAX_NAME_LEN, "%s", entry->d_name);
                    count++;
                }
            }
            closedir(dir);
        }
    }

    return count;
}

int app_loader_scan(char (*app_names)[APP_LOADER_MAX_NAME_LEN], int max_apps) {
    if (!s_cache_valid) {
        int64_t t0 = esp_timer_get_time();
        s_cache_count = scan_sd_card(s_cache_names, APP_LOADER_MAX_APPS);
        // Sort cache alphabetically (parallel sort of names + display names)
        for (int i = 0; i < s_cache_count - 1; i++) {
            for (int j = i + 1; j < s_cache_count; j++) {
                if (strcmp(s_cache_names[i], s_cache_names[j]) > 0) {
                    char tmp[APP_LOADER_MAX_NAME_LEN];
                    memcpy(tmp, s_cache_names[i], APP_LOADER_MAX_NAME_LEN);
                    memcpy(s_cache_names[i], s_cache_names[j], APP_LOADER_MAX_NAME_LEN);
                    memcpy(s_cache_names[j], tmp, APP_LOADER_MAX_NAME_LEN);
                    memcpy(tmp, s_cache_display_names[i], APP_LOADER_MAX_NAME_LEN);
                    memcpy(s_cache_display_names[i], s_cache_display_names[j], APP_LOADER_MAX_NAME_LEN);
                    memcpy(s_cache_display_names[j], tmp, APP_LOADER_MAX_NAME_LEN);
                }
            }
        }
        s_cache_valid = true;
        int64_t t1 = esp_timer_get_time();
        ESP_LOGI(TAG, "Scanned SD card: %d app(s) in %lld ms", s_cache_count, (t1 - t0) / 1000);
    } else {
        ESP_LOGI(TAG, "Using cached app list: %d app(s)", s_cache_count);
    }

    int copy_count = s_cache_count;
    if (copy_count > max_apps) copy_count = max_apps;
    for (int i = 0; i < copy_count; i++) {
        snprintf(app_names[i], APP_LOADER_MAX_NAME_LEN, "%s", s_cache_names[i]);
    }
    return s_cache_count;
}

const char *app_loader_get_cached_display_name(int index) {
    if (index < 0 || index >= s_cache_count) return "";
    return s_cache_display_names[index];
}

bool app_loader_load(const char *app_name) {
    ESP_LOGI(TAG, "Loading app: '%s'", app_name);

    app_context_t *ctx = os_get_current_app();

    if (!ctx) {
        ESP_LOGI(TAG, "Allocating new app context");
        ctx = app_calloc(1, sizeof(app_context_t));
        if (!ctx) {
            ESP_LOGE(TAG, "Failed to allocate app context");
            return false;
        }
        os_set_current_app(ctx);
    }

    // Try loading from SD card as ELF
    char elf_path[128];
    snprintf(elf_path, sizeof(elf_path), "/sdcard/apps/%s/program.elf", app_name);
    ESP_LOGI(TAG, "Trying ELF: %s", elf_path);

    elf_handle_t *handle = elf_loader_load(elf_path);
    if (!handle) {
        if (!sd_card_is_mounted()) {
            ESP_LOGW(TAG, "SD card not mounted, cannot load ELF apps");
        }
        ESP_LOGE(TAG, "Unknown app: %s", app_name);
        app_free(ctx);
        os_set_current_app(NULL);
        return false;
    }

    ESP_LOGI(TAG, "ELF loaded successfully from SD card");

    // Set up the app context from ELF symbols
    snprintf(ctx->name, sizeof(ctx->name), "%s", app_name);
    ctx->init = (app_init_fn)elf_loader_symbol(handle, "app_init");
    ctx->checkpoint = (app_checkpoint_fn)elf_loader_symbol(handle, "app_checkpoint");
    ctx->close = (app_close_fn)elf_loader_symbol(handle, "app_close");
    ctx->event_fn = (app_event_fn)elf_loader_symbol(handle, "app_event");
    ctx->handle = handle;
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    ctx->timer_interval_ms = 0;
    ctx->user_data = NULL;

    // Reset keyboard after flash operations to recover I2C bus,
    // but only when a keyboard is actually present.
    int64_t t_kbd0 = esp_timer_get_time();
    if (keyboard_is_available()) {
        keyboard_deinit();
        if (!keyboard_init()) {
            ESP_LOGW(TAG, "Keyboard reset failed; continuing without keyboard");
        }
    }
    int64_t t_kbd1 = esp_timer_get_time();
    ESP_LOGI(TAG, "  app_loader: keyboard_reset %lld ms", (t_kbd1 - t_kbd0) / 1000);

    if (!ctx->init) {
        ESP_LOGE(TAG, "ELF missing app_init entry point");
        elf_loader_unload(handle);
        app_free(ctx);
        os_set_current_app(NULL);
        return false;
    }

    // Check required capabilities from manifest
    app_sd_manifest_t *manifest = malloc(sizeof(app_sd_manifest_t));
    bool caps_ok = true;
    if (manifest) {
        app_manifest_read(app_name, manifest);
        if (manifest->requires[0]) {
            char caps[APP_MANIFEST_CAP_MAX];
            strncpy(caps, manifest->requires, sizeof(caps) - 1);
            caps[sizeof(caps) - 1] = '\0';
            char *token = strtok(caps, ",");
            while (token) {
                while (*token == ' ') token++;
                char *end = token + strlen(token);
                while (end > token && end[-1] == ' ') end--;
                *end = '\0';
                if (token[0] && !os_has_capability(token)) {
                    ESP_LOGE(TAG, "App '%s' requires capability '%s' which is not available",
                             app_name, token);
                    caps_ok = false;
                    break;
                }
                token = strtok(NULL, ",");
            }
        }
        free(manifest);
    }
    if (!caps_ok) {
        elf_loader_unload(handle);
        app_free(ctx);
        os_set_current_app(NULL);
        return false;
    }

    if (!config_bind_app(ctx->name)) {
        ESP_LOGW(TAG, "Failed to bind config namespace for app %s", ctx->name);
    }
    int64_t t_init0 = esp_timer_get_time();
    ctx->init(ctx);
    int64_t t_init1 = esp_timer_get_time();
    ESP_LOGI(TAG, "  app_loader: app_init %lld ms", (t_init1 - t_init0) / 1000);
    ESP_LOGI(TAG, "%s loaded from SD card and initialized", app_name);
    return true;
}

int app_loader_get_count(void) {
    static char names[APP_LOADER_MAX_APPS][APP_LOADER_MAX_NAME_LEN];
    return app_loader_scan(names, APP_LOADER_MAX_APPS);
}
