#include "arduboy_tunes.h"
#include <stdint.h>
#include <stdio.h>

// Global instance is defined in the Numbers game, not here
// ArduboyTunes tunes;

void ArduboyTunes::initChannel(uint8_t pin) {
    initialized = true;
}

void ArduboyTunes::tone(uint16_t frequency, uint16_t duration) {
    // Sound stub
}