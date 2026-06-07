// Test os_log
#include "os_core.h"

void app_init(app_context_t *ctx) {
    // Test if os_log works
    os_log("test", "Testing os_log");
}

void app_event(app_context_t *ctx, event_t *event) {
}

void app_checkpoint(app_context_t *ctx) {
}

void app_close(app_context_t *ctx) {
}