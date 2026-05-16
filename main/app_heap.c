#include "app_heap.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "multi_heap.h"

#include <string.h>

static const char *TAG = "app_heap";

#define APP_HEAP_SIZE (96 * 1024)

static void *app_heap_storage = NULL;
static size_t app_heap_size = 0;
static multi_heap_handle_t app_heap = NULL;

// Temporary expansion support
static void *temp_heap_storage = NULL;
static size_t temp_heap_size = 0;
static multi_heap_handle_t temp_heap = NULL;

static bool app_heap_ensure_ready(void) {
    if (app_heap) {
        return true;
    }

    if (!app_heap_init()) {
        return false;
    }

    return app_heap != NULL;
}

void app_heap_log_stats(const char *label) {
    if (!app_heap_storage || !app_heap) {
        ESP_LOGI(TAG, "%s: inactive", label ? label : "stats");
        return;
    }

    multi_heap_info_t info;
    multi_heap_get_info(app_heap, &info);

    ESP_LOGI(TAG,
             "%s: free=%u largest=%u allocated=%u min_free=%u blocks=%u/%u",
             label ? label : "stats",
             (unsigned)info.total_free_bytes,
             (unsigned)info.largest_free_block,
             (unsigned)info.total_allocated_bytes,
             (unsigned)info.minimum_free_bytes,
             (unsigned)info.allocated_blocks,
             (unsigned)info.total_blocks);
}

bool app_heap_init(void) {
    if (app_heap) {
        return true;
    }

    if (!app_heap_storage) {
        app_heap_storage = heap_caps_malloc(APP_HEAP_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!app_heap_storage) {
            ESP_LOGE(TAG, "Failed to reserve %u bytes for app heap", (unsigned)APP_HEAP_SIZE);
            return false;
        }
        app_heap_size = APP_HEAP_SIZE;
    }

    app_heap = multi_heap_register(app_heap_storage, app_heap_size);
    if (!app_heap) {
        ESP_LOGE(TAG, "Failed to initialize app heap");
        return false;
    }

    ESP_LOGI(TAG, "App heap ready: %u bytes", (unsigned)app_heap_size);
    app_heap_log_stats("after init");
    return true;
}

void app_heap_reset(void) {
    if (!app_heap_storage) {
        return;
    }

    memset(app_heap_storage, 0, app_heap_size);
    app_heap = NULL;
    app_heap = multi_heap_register(app_heap_storage, app_heap_size);
    if (!app_heap) {
        ESP_LOGE(TAG, "Failed to reset app heap");
        return;
    }

    app_heap_log_stats("after reset");
}

void app_heap_release(void) {
    if (!app_heap_storage) {
        return;
    }

    ESP_LOGI(TAG, "releasing app heap (%u bytes)", (unsigned)app_heap_size);
    heap_caps_free(app_heap_storage);
    app_heap_storage = NULL;
    app_heap_size = 0;
    app_heap = NULL;
}

void *app_malloc(size_t size) {
    if (!app_heap_ensure_ready()) {
        return NULL;
    }
    return multi_heap_malloc(app_heap, size);
}

void *app_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return app_malloc(0);
    }

    if (SIZE_MAX / count < size) {
        return NULL;
    }

    size_t total_size = count * size;
    void *ptr = app_malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void *app_realloc(void *ptr, size_t size) {
    if (!ptr) {
        return app_malloc(size);
    }

    if (size == 0) {
        app_free(ptr);
        return NULL;
    }

    if (!app_heap_ensure_ready()) {
        return NULL;
    }

    return multi_heap_realloc(app_heap, ptr, size);
}

void app_free(void *ptr) {
    if (!ptr || !app_heap_ensure_ready()) {
        return;
    }

    multi_heap_free(app_heap, ptr);
}