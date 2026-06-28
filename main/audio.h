#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the audio hardware (I2S DAC).
bool audio_init(void);

// Deinitialize audio hardware.
void audio_deinit(void);

// Whether audio output is available on this board.
bool audio_is_available(void);

// Play a tone at the given frequency for duration_ms (non-blocking).
// frequency_hz: 100-20000 Hz typically
// duration_ms: how long to play (0 = until audio_stop)
void audio_beep(int frequency_hz, int duration_ms);

// Play raw 16-bit signed PCM mono samples at 44100 Hz (non-blocking).
// The buffer must remain valid until audio_is_playing() returns false.
// Returns true if playback started.
bool audio_play(const int16_t *samples, size_t sample_count);

// Stop all audio playback.
void audio_stop(void);

// Whether audio is currently playing.
bool audio_is_playing(void);

// Volume control (0-100, default 70).
void audio_set_volume(int volume);
int audio_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_H
