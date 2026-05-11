#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdbool.h>
#include "os_core.h"

// Forward declaration for LovyanGFX display (C++ compatible)
#ifdef __cplusplus
class LGFX;
extern LGFX* display_tft;
#else
typedef void LGFX;  // Opaque pointer for C
extern LGFX* display_tft;
#endif

// Hardware initialization
#ifdef __cplusplus
extern "C" {
#endif

bool hardware_init(void);

// Display functions
bool display_init(void);
void display_clear(uint16_t color);
void display_draw_text(int x, int y, const char *text, uint16_t color);
void display_draw_pixel(int x, int y, uint16_t color);

// Touch functions
bool touch_init(void);
bool touch_read(uint16_t *x, uint16_t *y, bool *pressed);

// Keyboard functions
bool keyboard_init(void);
bool keyboard_read_event(event_t *event);

// Timer functions
bool timer_init(void);
void timer_set_interval(uint32_t interval_ms);

// Serial functions
bool serial_init(void);
size_t serial_read(char *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_H
