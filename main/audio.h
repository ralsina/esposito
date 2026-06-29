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

// --- Streaming API for continuous playback (e.g., file decoding) ---
// These bypass the one-shot audio_play() and give direct I2S access.

// Enable I2S DAC for streaming. Returns false if busy or unavailable.
bool audio_stream_start(void);

// Write interleaved stereo 16-bit frames at 44100 Hz to the DAC.
// Blocks until the DMA buffer accepts the data.
// frame_count: number of stereo frames (each = 2 x int16_t)
// Returns false if stream was stopped.
bool audio_stream_write(const int16_t *stereo_frames, size_t frame_count);

// Drain remaining DMA data and disable I2S DAC.
void audio_stream_stop(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_H
