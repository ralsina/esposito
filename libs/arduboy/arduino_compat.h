#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <os_core.h>

// Declare C functions we need
extern "C" int rand(void);
extern "C" void srand(unsigned int);

// Arduino type definitions
#define boolean bool
#define uint8  uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define int8   int8_t
#define int16  int16_t
#define int32  int32_t

// Arduino constants
#define HIGH 1
#define LOW  0

// Arduino pin definitions (not used, but for compatibility)
#define PIN_SPEAKER_1  0
#define PIN_SPEAKER_2  1

// Arduino math functions - Numbers game uses rand() directly

// Arduino string conversion
#define dtostrf(value, width, precision, buffer) sprintf(buffer, "%*.*f", width, precision, value)

// Forward declarations for Esposito integration
typedef struct app_context app_context_t;

extern "C" {
void app_init(app_context_t *ctx);
void app_event(app_context_t *ctx, event_t *event);
void app_checkpoint(app_context_t *ctx);
void app_close(app_context_t *ctx);
}

// Arduino setup/loop declarations - to be provided by the game
void setup();
void loop();

#endif // ARDUINO_COMPAT_H