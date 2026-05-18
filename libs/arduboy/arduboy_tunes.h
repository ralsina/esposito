#ifndef ARDUBOY_TUNES_H
#define ARDUBOY_TUNES_H

#include <stdint.h>
#include <stdbool.h>

// C++ ArduboyTunes class (mimics original Arduino API)
class ArduboyTunes {
public:
    bool initialized;

    // Sound API methods
    void initChannel(uint8_t pin);
    void tone(uint16_t frequency, uint16_t duration);
};

#endif // ARDUBOY_TUNES_H