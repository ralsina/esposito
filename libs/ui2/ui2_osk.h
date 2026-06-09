#ifndef UI2_OSK_H
#define UI2_OSK_H

#include <stdbool.h>
#include <stdint.h>
#include "os_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI2_OSK_RESULT_CONFIRMED,
    UI2_OSK_RESULT_CANCELLED,
} ui2_osk_result_t;

bool ui2_osk_input_text(
    const char *title,
    char *buffer,
    int max_len,
    const char *initial_text,
    bool mask_input
);

bool ui2_osk_handle_event(app_context_t *ctx, event_t *event);

bool ui2_osk_is_active(void);

ui2_osk_result_t ui2_osk_get_result(void);

#ifdef __cplusplus
}
#endif

#endif
