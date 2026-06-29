#include "audio.h"
#include "hardware_config.h"

#if defined(BOARD_HAS_AUDIO) && BOARD_HAS_AUDIO

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include <math.h>
#include <string.h>

static const char *TAG = "audio";

static i2s_chan_handle_t tx_handle = NULL;
static bool audio_initialized = false;
static bool audio_playing = false;
static int audio_volume = 70;

#define SAMPLE_RATE 44100

// Lookup table for fast sine generation (256 entries, 0-65535)
static uint16_t sine_table[256];
static bool sine_table_ready = false;

static void init_sine_table(void) {
    for (int i = 0; i < 256; i++) {
        sine_table[i] = (uint16_t)((sinf((float)i * 2.0f * M_PI / 256.0f) * 0.5f + 0.5f) * 65535.0f);
    }
    sine_table_ready = true;
}

static inline int16_t apply_volume(int16_t sample) {
    return (int16_t)((int32_t)sample * audio_volume / 100);
}

// Enable the I2S channel (powers on the DAC). Called before playback.
static esp_err_t audio_enable(void) {
    return i2s_channel_enable(tx_handle);
}

// Disable the I2S channel (powers off the DAC). Called after playback ends.
static void audio_disable(void) {
    i2s_channel_disable(tx_handle);
}

// --- Public API ---

bool audio_init(void) {
    if (audio_initialized) return true;

    ESP_LOGI(TAG, "Initializing I2S audio (BCLK=%d LRCK=%d DOUT=%d)",
             BOARD_I2S_BCLK, BOARD_I2S_LRCK, BOARD_I2S_DOUT);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .bclk = BOARD_I2S_BCLK,
            .ws   = BOARD_I2S_LRCK,
            .dout = BOARD_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .mclk = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return false;
    }

    // Don't enable yet — enabling the channel powers on the DAC which causes
    // an audible pop. We enable only during playback and disable after.

    if (!sine_table_ready) init_sine_table();

    audio_initialized = true;
    ESP_LOGI(TAG, "Audio ready (I2S %d-bit stereo @ %d Hz)", 16, SAMPLE_RATE);
    return true;
}

void audio_deinit(void) {
    audio_stop();
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }
    audio_initialized = false;
}

bool audio_is_available(void) {
    return audio_initialized;
}

// --- Tone generation task ---

static void beep_task(void *arg) {
    int raw = (int)(intptr_t)arg;
    int duration_ms = raw >> 16;
    int freq = raw & 0xFFFF;
    if (freq < 1) freq = 440;

    ESP_LOGI(TAG, "beep_task: freq=%d duration=%d playing=%d", freq, duration_ms, audio_playing);

    audio_enable();

    int total_samples = SAMPLE_RATE * duration_ms / 1000;
    // Short fade-in to avoid pop (10ms ramp from 0 to full volume)
    int fade_samples = SAMPLE_RATE * 10 / 1000;
    if (fade_samples > total_samples / 2) fade_samples = total_samples / 2;

    uint32_t phase = 0;
    uint32_t phase_step = (uint32_t)((uint64_t)freq * 256ULL * 65536ULL / SAMPLE_RATE);

    int samples_written = 0;
    int16_t buf[480];
    const int frames_per_write = 240;

    while (audio_playing && samples_written < total_samples) {
        for (int i = 0; i < frames_per_write; i++) {
            int16_t val = (int16_t)((int32_t)sine_table[phase >> 24] - 32768);
            // Fade-in at start, fade-out at end
            int remaining = total_samples - samples_written - i;
            int envelope;
            if (samples_written + i < fade_samples)
                envelope = (samples_written + i) * 100 / fade_samples;
            else if (remaining < fade_samples)
                envelope = remaining * 100 / fade_samples;
            else
                envelope = 100;
            val = (int16_t)((int32_t)val * envelope / 100);
            val = apply_volume(val);
            buf[i * 2]     = val;
            buf[i * 2 + 1] = val;
            phase += phase_step;
        }
        size_t bytes_written = 0;
        esp_err_t wret = i2s_channel_write(tx_handle, buf, sizeof(buf), &bytes_written, 1000);
        if (wret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write failed: %s", esp_err_to_name(wret));
            break;
        }
        samples_written += frames_per_write;
    }

    // Drain the DMA buffer before disabling
    vTaskDelay(pdMS_TO_TICKS(50));
    audio_disable();

    ESP_LOGI(TAG, "beep_task: done, wrote %d samples", samples_written);
    audio_playing = false;
    vTaskDelete(NULL);
}

void audio_beep(int frequency_hz, int duration_ms) {
    if (!audio_initialized || audio_playing) return;
    if (frequency_hz < 50 || frequency_hz > 20000) return;

    audio_playing = true;
    int arg = (frequency_hz & 0xFFFF) | (duration_ms << 16);
    xTaskCreate(beep_task, "audio_beep", 4096, (void *)(intptr_t)arg, 2, NULL);
}

// --- Buffer playback task ---

static const int16_t *play_buffer = NULL;
static size_t play_count = 0;

static void play_task(void *arg) {
    audio_enable();

    size_t pos = 0;
    int16_t buf[480];
    const int frames_per_write = 240;

    while (audio_playing && pos < play_count) {
        int to_write = play_count - pos;
        if (to_write > frames_per_write) to_write = frames_per_write;

        for (int i = 0; i < to_write; i++) {
            int16_t val = apply_volume(play_buffer[pos + i]);
            buf[i * 2]     = val;
            buf[i * 2 + 1] = val;
        }

        size_t bytes_written;
        i2s_channel_write(tx_handle, buf, to_write * 4, &bytes_written, portMAX_DELAY);
        pos += to_write;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    audio_disable();

    audio_playing = false;
    vTaskDelete(NULL);
}

bool audio_play(const int16_t *samples, size_t sample_count) {
    if (!audio_initialized || audio_playing || !samples || sample_count == 0) {
        return false;
    }

    play_buffer = samples;
    play_count = sample_count;
    audio_playing = true;

    BaseType_t ret = xTaskCreate(play_task, "audio_play", 4096, NULL, 2, NULL);
    if (ret != pdPASS) {
        audio_playing = false;
        return false;
    }
    return true;
}

void audio_stop(void) {
    audio_playing = false;
    vTaskDelay(pdMS_TO_TICKS(60));
    if (tx_handle) audio_disable();
}

bool audio_is_playing(void) {
    return audio_playing;
}

void audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    audio_volume = volume;
}

int audio_get_volume(void) {
    return audio_volume;
}

// --- Streaming API ---

bool audio_stream_start(void) {
    if (!audio_initialized || audio_playing) return false;
    audio_playing = true;
    audio_enable();
    return true;
}

bool audio_stream_write(const int16_t *stereo_frames, size_t frame_count) {
    if (!audio_initialized || !audio_playing) return false;

    int16_t buf[512];
    const size_t max_frames = sizeof(buf) / 4;
    size_t pos = 0;

    while (pos < frame_count && audio_playing) {
        size_t chunk = frame_count - pos;
        if (chunk > max_frames) chunk = max_frames;

        for (size_t i = 0; i < chunk; i++) {
            buf[i * 2]     = apply_volume(stereo_frames[(pos + i) * 2]);
            buf[i * 2 + 1] = apply_volume(stereo_frames[(pos + i) * 2 + 1]);
        }

        size_t bytes_written;
        esp_err_t ret = i2s_channel_write(tx_handle, buf, chunk * 4, &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) return false;
        pos += chunk;
    }
    return true;
}

void audio_stream_stop(void) {
    if (!audio_initialized) return;
    vTaskDelay(pdMS_TO_TICKS(50));
    audio_disable();
    audio_playing = false;
}

#else

// Stubs for boards without audio hardware.
bool audio_init(void) { return false; }
void audio_deinit(void) {}
bool audio_is_available(void) { return false; }
void audio_beep(int frequency_hz, int duration_ms) { (void)frequency_hz; (void)duration_ms; }
bool audio_play(const int16_t *samples, size_t sample_count) { (void)samples; (void)sample_count; return false; }
void audio_stop(void) {}
bool audio_is_playing(void) { return false; }
void audio_set_volume(int volume) { (void)volume; }
int audio_get_volume(void) { return 0; }

bool audio_stream_start(void) { return false; }
bool audio_stream_write(const int16_t *stereo_frames, size_t frame_count) { (void)stereo_frames; (void)frame_count; return false; }
void audio_stream_stop(void) {}

#endif // BOARD_HAS_AUDIO
