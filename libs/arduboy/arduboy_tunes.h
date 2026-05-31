#ifndef ARDUBOY_TUNES_H
#define ARDUBOY_TUNES_H

#include <stdint.h>
#include <stdbool.h>

class ArduboyTunes {
public:
    bool initialized;

    void initChannel(uint8_t pin);
    void tone(uint16_t frequency, uint16_t duration);
};

class ArduboyTones {
public:
    bool &audio_enabled;

    ArduboyTones(bool &enabled);
    void tone(uint16_t freq, uint16_t duration);
    void tones(const uint16_t *data);
};

#endif // ARDUBOY_TUNES_H