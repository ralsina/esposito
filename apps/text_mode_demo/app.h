// Text Mode Demo App Header

#ifndef APP_TEXT_MODE_DEMO_H
#define APP_TEXT_MODE_DEMO_H

#include "os_core.h"

// App implementation
void text_mode_demo_app_init(app_context_t *ctx);
void text_mode_demo_app_checkpoint(app_context_t *ctx);
void text_mode_demo_app_close(app_context_t *ctx);
void text_mode_demo_app_event(app_context_t *ctx, event_t *event);

#endif // APP_TEXT_MODE_DEMO_H