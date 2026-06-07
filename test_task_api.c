// Test OS task API
#include "os_core.h"

void app_init(app_context_t *ctx) {
    // Test if we can call the OS task API functions
    os_task_handle_t *task = os_task_create(NULL, "test", 2048, 5, 0);
    if (task) {
        os_task_delete(task);
    }

    os_semaphore_handle_t *sem = os_semaphore_create();
    if (sem) {
        os_semaphore_give(sem);
        os_semaphore_take(sem, 100);
        os_semaphore_delete(sem);
    }
}

void app_event(app_context_t *ctx, event_t *event) {
}

void app_checkpoint(app_context_t *ctx) {
}

void app_close(app_context_t *ctx) {
}