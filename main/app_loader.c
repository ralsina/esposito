#include "app_loader.h"
#include "app_heap.h"
#include "app_config.h"
#include "app_manifest.h"
#include "os_core.h"
#include "elf_loader.h"
#include "sd_card.h"
#include "checkpoint.h"
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

int app_loader_scan(char (*app_names)[64], int max_apps) {
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
                    app_manifest_read(entry->d_name, &manifest);
                    if (!manifest.show_in_launcher) continue;
                    strncpy(app_names[count], entry->d_name, 63);
                    app_names[count][63] = '\0';
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
    ESP_LOGI(TAG, "🔧 app_loader_load called with: '%s'", app_name);

    app_context_t *ctx = os_get_current_app();
    ESP_LOGI(TAG, "🔧 os_get_current_app returned: %p", (void*)ctx);

    if (!ctx) {
        ESP_LOGE(TAG, "❌ No app context available - need to create one");
        // Create a new app context
        ctx = app_calloc(1, sizeof(app_context_t));
        if (!ctx) {
            ESP_LOGE(TAG, "❌ Failed to allocate app context");
            return false;
        }
        ESP_LOGI(TAG, "✅ Created new app context at %p", (void*)ctx);

        // Set it as the current app
        os_set_current_app(ctx);
        ESP_LOGI(TAG, "✅ Set as current app");
    }

    // Try loading from SD card as ELF
    char elf_path[128];
    snprintf(elf_path, sizeof(elf_path), "/sdcard/apps/%s/program.elf", app_name);
    ESP_LOGI(TAG, "Trying ELF: %s", elf_path);

    elf_handle_t *handle = elf_loader_load(elf_path);
    if (!handle) {
        // Check if SD card is even mounted
        if (!sd_card_is_mounted()) {
            ESP_LOGW(TAG, "SD card not mounted, cannot load ELF apps");
        }
        ESP_LOGE(TAG, "❌ Unknown app: %s", app_name);
        return false;
    }

    ESP_LOGI(TAG, "ELF loaded successfully from SD card");

    // Set up the app context from ELF symbols
    strcpy(ctx->name, app_name);
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
        return false;
    }

    checkpoint_open(ctx->name);
    if (!config_bind_app(ctx->name)) {
        ESP_LOGW(TAG, "Failed to bind config namespace for app %s", ctx->name);
    }
    ctx->init(ctx);
    ESP_LOGI(TAG, "✅ %s loaded from SD card and initialized", app_name);
    return true;
}

int app_loader_get_count(void) {
    char names[APP_LOADER_MAX_APPS][64];
    return app_loader_scan(names, APP_LOADER_MAX_APPS);
}
