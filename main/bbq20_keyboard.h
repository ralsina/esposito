#ifndef BBQ20_KEYBOARD_H
#define BBQ20_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// BBQ20/BBQ10 Keyboard I2C Address
#define BBQ20_I2C_ADDR     0x1F

// bt2i2c extended keycodes (0x80+ range) — protocol extension beyond BBQ20.
// Used for keys from Bluetooth HID keyboards via the bt2i2c bridge.
#define BT2I2C_KEY_ESC         0x80
#define BT2I2C_KEY_F1          0x81
#define BT2I2C_KEY_F2          0x82
#define BT2I2C_KEY_F3          0x83
#define BT2I2C_KEY_F4          0x84
#define BT2I2C_KEY_F5          0x85
#define BT2I2C_KEY_F6          0x86
#define BT2I2C_KEY_F7          0x87
#define BT2I2C_KEY_F8          0x88
#define BT2I2C_KEY_F9          0x89
#define BT2I2C_KEY_F10         0x8A
#define BT2I2C_KEY_F11         0x8B
#define BT2I2C_KEY_F12         0x8C
#define BT2I2C_KEY_PRTSCR      0x8D
#define BT2I2C_KEY_SCRLK       0x8E
#define BT2I2C_KEY_PAUSE       0x8F
#define BT2I2C_KEY_INSERT      0x90
#define BT2I2C_KEY_HOME        0x91
#define BT2I2C_KEY_PGUP        0x92
#define BT2I2C_KEY_DELETE      0x93
#define BT2I2C_KEY_END         0x94
#define BT2I2C_KEY_PGDN        0x95
#define BT2I2C_KEY_RIGHT       0x96
#define BT2I2C_KEY_LEFT        0x97
#define BT2I2C_KEY_DOWN        0x98
#define BT2I2C_KEY_UP          0x99

// Key state structure for BBQ20 keyboard events
typedef struct {
    uint8_t key_code;      // Mapped key code delivered to apps (ASCII/control)
    uint8_t raw_key_code;  // Raw keyboard scan code from BBQ20 FIFO
    uint8_t modifiers;     // Modifier bitmask (Ctrl/Alt/Fn/Fn2)
    bool pressed;          // True for key down, false for key up
} bbq20_key_event_t;

// Initialize BBQ20 keyboard (I2C or fallback to fake keyboard)
bool bbq20_keyboard_init(void);

// Deinitialize BBQ20 keyboard (tears down I2C bus)
void bbq20_keyboard_deinit(void);

// Read key events from BBQ20 keyboard (real or fake fallback)
bool bbq20_read_key_event(bbq20_key_event_t *event);

// Convert BBQ20 key code to ASCII character
char bbq20_key_to_ascii(uint8_t key_code, uint8_t state);

// Get current modifier key state
uint8_t bbq20_get_modifiers(void);

// Keyboard backlight control (0 = off, 255 = max)
void bbq20_set_backlight(uint8_t brightness);
uint8_t bbq20_get_backlight(void);

#ifdef __cplusplus
}
#endif

#endif // BBQ20_KEYBOARD_H