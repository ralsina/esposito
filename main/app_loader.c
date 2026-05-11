#include "app_loader.h"
#include "os_core.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "app_loader";

// Built-in test app declarations
extern void app_init(app_context_t *ctx);
extern void app_checkpoint(app_context_t *ctx);
extern void app_close(app_context_t *ctx);
extern void app_event(app_context_t *ctx, event_t *event);

bool app_loader_init(void) {
    ESP_LOGI(TAG, "App loader initialized");
    return true;
}

int app_loader_scan(char (*app_names)[64], int max_apps) {
    // For now, just return the built-in test app
    if (max_apps >= 1) {
        strcpy(app_names[0], "key_echo");
    }
    return 1;
}

bool app_loader_load(const char *app_name) {
    ESP_LOGI(TAG, "🔧 app_loader_load called with: '%s'", app_name);

    // For testing, load the built-in key_echo app directly
    if (strcmp(app_name, "key_echo") == 0) {
        ESP_LOGI(TAG, "🔧 Loading built-in key_echo app");

        app_context_t *ctx = os_get_current_app();
        ESP_LOGI(TAG, "🔧 os_get_current_app returned: %p", (void*)ctx);

        if (!ctx) {
            ESP_LOGE(TAG, "❌ No app context available - need to create one");
            // Create a new app context
            ctx = calloc(1, sizeof(app_context_t));
            if (!ctx) {
                ESP_LOGE(TAG, "❌ Failed to allocate app context");
                return false;
            }
            ESP_LOGI(TAG, "✅ Created new app context at %p", (void*)ctx);

            // Set it as the current app
            os_set_current_app(ctx);
            ESP_LOGI(TAG, "✅ Set as current app");
        }

        // Set up the app context
        strcpy(ctx->name, "key_echo");
        ctx->init = app_init;
        ctx->checkpoint = app_checkpoint;
        ctx->close = app_close;
        ctx->event_fn = app_event;
        ctx->subscriptions = EVENT_KEYBOARD;
        ctx->timer_interval_ms = 0;
        ctx->user_data = NULL;

        ESP_LOGI(TAG, "🔧 App context configured, calling init function at %p", (void*)ctx->init);

        // Initialize the app
        ctx->init(ctx);
        ESP_LOGI(TAG, "✅ key_echo app loaded and initialized");
        return true;
    }

    ESP_LOGE(TAG, "❌ Unknown app: %s", app_name);
    return false;
}

int app_loader_get_count(void) {
    return 1; // One built-in app
}
