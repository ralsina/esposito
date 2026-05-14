#ifndef APP_HEAP_H
#define APP_HEAP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool app_heap_init(void);
void app_heap_reset(void);
void app_heap_release(void);
void app_heap_log_stats(const char *label);

void *app_malloc(size_t size);
void *app_calloc(size_t count, size_t size);
void *app_realloc(void *ptr, size_t size);
void app_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif