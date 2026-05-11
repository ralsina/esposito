#ifndef BBQ20_KEYBOARD_H
#define BBQ20_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// BBQ20/BBQ10 Keyboard I2C Address
#define BBQ20_I2C_ADDR     0x1F

// Key state structure for BBQ20 keyboard events
typedef struct {
    uint8_t key_code;      // Raw key code or ASCII character
    uint8_t modifiers;     // Key state (down, up, repeat, caps lock, num lock)
    bool pressed;         // True for key down, false for key up
} bbq20_key_event_t;

// Initialize BBQ20 keyboard (I2C or fallback to fake keyboard)
bool bbq20_keyboard_init(void);

// Read key events from BBQ20 keyboard (real or fake fallback)
bool bbq20_read_key_event(bbq20_key_event_t *event);

// Convert BBQ20 key code to ASCII character
char bbq20_key_to_ascii(uint8_t key_code, uint8_t state);

#ifdef __cplusplus
}
#endif

#endif // BBQ20_KEYBOARD_H