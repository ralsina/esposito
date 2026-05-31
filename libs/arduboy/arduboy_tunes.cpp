#include "arduboy_tunes.h"
#include <stdint.h>
#include <stdio.h>

void ArduboyTunes::initChannel(uint8_t pin) {
    initialized = true;
}

void ArduboyTunes::tone(uint16_t frequency, uint16_t duration) {
}

ArduboyTones::ArduboyTones(bool &enabled) : audio_enabled(enabled) {
}

void ArduboyTones::tone(uint16_t freq, uint16_t duration) {
}

void ArduboyTones::tones(const uint16_t *data) {
}