#ifndef UI_OSK_H
#define UI_OSK_H

#include <stdbool.h>
#include "os_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// OSK result codes
typedef enum {
    UI_OSK_RESULT_CONFIRMED,   // User pressed Enter
    UI_OSK_RESULT_CANCELLED,   // User pressed ESC
} ui_osk_result_t;

// Main API - starts OSK input session
// Returns true if OSK was started successfully
bool ui_osk_input_text(
    const char *title,        // Title/prompt for the input
    char *buffer,             // User-provided buffer for result
    int max_len,              // Maximum buffer length
    const char *initial_text, // Optional initial text (NULL for empty)
    bool mask_input           // Whether to mask input (passwords)
);

// Event handler for OSK - call from app's event loop
// Returns true if event was handled by OSK
bool ui_osk_handle_event(app_context_t *ctx, event_t *event);

// Check if OSK is currently active
bool ui_osk_is_active(void);

// Get the result of the last OSK session
ui_osk_result_t ui_osk_get_result(void);

#ifdef __cplusplus
}
#endif

#endif // UI_OSK_H
