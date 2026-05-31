#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <os_core.h>

extern "C" int rand(void);
extern "C" void srand(unsigned int);
extern "C" bool keyboard_read_event(event_t *event);

static inline int64_t esp_timer_get_time(void) {
    uint32_t ccount;
    __asm__ volatile("rsr %0, ccount" : "=r"(ccount));
    // CCOUNT increments at CPU frequency (240 MHz). Convert to microseconds.
    // ccount / 240 = microseconds. Use multiplication to avoid division.
    return (int64_t)ccount * 100 / 24000;
}

#define boolean bool
#define uint8  uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define int8   int8_t
#define int16  int16_t
#define int32  int32_t

#define HIGH 1
#define LOW  0

#define PIN_SPEAKER_1  0
#define PIN_SPEAKER_2  1

#define PROGMEM
#define WHITE 1
#define BLACK 0
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TONES_END 0xFFFF
#define byte uint8_t
#define Arduboy2 Arduboy

template <typename T>
static inline T min(T left, T right) {
	return (left < right) ? left : right;
}

template <typename T>
static inline T max(T left, T right) {
	return (left > right) ? left : right;
}

template <typename T>
static inline T constrain(T value, T lower, T upper) {
	return min(max(value, lower), upper);
}

#define dtostrf(value, width, precision, buffer) sprintf(buffer, "%*.*f", width, precision, value)

typedef struct app_context app_context_t;

extern "C" {
void app_init(app_context_t *ctx);
void app_event(app_context_t *ctx, event_t *event);
void app_checkpoint(app_context_t *ctx);
void app_close(app_context_t *ctx);
}

void setup();
void loop();

inline unsigned long millis() {
    return (unsigned long)(esp_timer_get_time() / 1000);
}

inline void delay(unsigned long ms) {
    // Simple busy-wait: keyboard events will be consumed by pollButtons() in the game loop
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) / 1000 < (int64_t)ms) {
        // Empty busy wait
    }
}

inline long random(long max) {
    return rand() % max;
}

inline long random(long min, long max) {
    return min + (rand() % (max - min));
}

#endif // ARDUINO_COMPAT_H