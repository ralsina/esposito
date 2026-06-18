#include "app_loader.h"
#include "app_heap.h"
#include "app_config.h"
#include "app_manifest.h"
#include "os_core.h"
#include "elf_loader.h"
#include "sd_card.h"
#include "hardware.h"
#include "esp_log.h"
#include <string.h>
#include <dirent.h>
#include <stdio.h>

static const char *TAG = "app_loader";

bool app_loader_init(void) {
    ESP_LOGI(TAG, "App loader initialized");
    return true;
}

int app_loader_scan(char (*app_names)[APP_LOADER_MAX_NAME_LEN], int max_apps) {
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
                    app_sd_manifest_t *manifest = malloc(sizeof(app_sd_manifest_t));
                    bool show = true;
                    if (manifest) {
                        app_manifest_read(entry->d_name, manifest);
                        show = manifest->show_in_launcher;
                        free(manifest);
                    }
                    if (!show) continue;
                    snprintf(app_names[count], APP_LOADER_MAX_NAME_LEN, "%s", entry->d_name);
                    count++;
                }
            }
            closedir(dir);
        }
    }

    ESP_LOGI(TAG, "Found %d app(s) on SD card", count);
    return count;
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
    if (keyboard_is_available()) {
        keyboard_deinit();
        if (!keyboard_init()) {
            ESP_LOGW(TAG, "Keyboard reset failed; continuing without keyboard");
        }
    }

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
    ctx->init(ctx);
    ESP_LOGI(TAG, "%s loaded from SD card and initialized", app_name);
    return true;
}

int app_loader_get_count(void) {
    static char names[APP_LOADER_MAX_APPS][APP_LOADER_MAX_NAME_LEN];
    return app_loader_scan(names, APP_LOADER_MAX_APPS);
}
