#include "app_launcher.h"
#include "os_core.h"
#include "esp_log.h"

static const char *TAG = "app_launcher";

void app_launcher_start(void) {
    ESP_LOGI(TAG, "Launching launcher app");
    os_load_app("launcher");
}

bool app_launcher_is_active(void) {
    return false;
}
