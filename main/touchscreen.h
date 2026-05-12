#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize XPT2046 touchscreen driver
bool touchscreen_init(void);

// Check if touchscreen is available
bool touchscreen_is_available(void);

// Get current touch position and state
// Returns true if touch data is valid, false otherwise
bool touchscreen_get_position(uint16_t *x, uint16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif

#endif // TOUCHSCREEN_H