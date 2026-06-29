#include "os_core.h"
#include "text_mode.h"
#include "ui2.h"
#include "ui2_toolbar.h"
#include "lucide_icons.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "wav_player";

#define MAX_FILES 64
#define MAX_PATH 256
#define CHUNK_FRAMES 1024

typedef enum {
    STATE_BROWSE,
    STATE_PLAYING,
    STATE_FINISHED,
} app_state_t;

static app_state_t state = STATE_BROWSE;
static char cwd[MAX_PATH] = "/sdcard/audio";
static char file_paths[MAX_FILES][MAX_PATH];
static char file_names[MAX_FILES][MAX_PATH];
static const char *file_items[MAX_FILES];
static int file_count = 0;
static ui2_list_t *file_list;
static ui2_layout_t *toolbar;

// WAV playback state
static char playing_name[64];
static volatile uint32_t wav_position;
static uint32_t wav_data_size;
static uint16_t wav_channels;
static uint32_t wav_sample_rate;
static uint16_t wav_bits_per_sample;
static volatile bool playback_done = false;
static char playback_error[64];
static char g_play_path[MAX_PATH];

// --- WAV header parsing ---

static bool parse_wav_header(FILE *f, uint32_t *out_data_offset, uint32_t *out_data_size,
                              uint16_t *out_channels, uint32_t *out_sample_rate,
                              uint16_t *out_bits) {
    uint8_t buf[12];
    if (fread(buf, 1, 12, f) != 12) return false;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return false;

    bool got_fmt = false, got_data = false;

    while (!got_data) {
        uint8_t chunk_hdr[8];
        if (fread(chunk_hdr, 1, 8, f) != 8) return false;

        uint32_t chunk_size = chunk_hdr[4] | (chunk_hdr[5] << 8) |
                              (chunk_hdr[6] << 16) | (chunk_hdr[7] << 24);

        if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            uint32_t to_read = chunk_size < 16 ? chunk_size : 16;
            if (fread(fmt, 1, to_read, f) != to_read) return false;

            uint16_t audio_format = fmt[0] | (fmt[1] << 8);
            if (audio_format != 1) return false;

            *out_channels    = fmt[2] | (fmt[3] << 8);
            *out_sample_rate = fmt[4] | (fmt[5] << 8) |
                               (fmt[6] << 16) | (fmt[7] << 24);
            *out_bits        = fmt[14] | (fmt[15] << 8);

            if (*out_bits != 16) return false;
            if (*out_channels < 1 || *out_channels > 2) return false;

            if (chunk_size > to_read)
                fseek(f, chunk_size - to_read, SEEK_CUR);
            got_fmt = true;
        } else if (memcmp(chunk_hdr, "data", 4) == 0) {
            if (!got_fmt) return false;
            *out_data_offset = (uint32_t)ftell(f);
            *out_data_size = chunk_size;
            got_data = true;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }

        if (chunk_size & 1) fseek(f, 1, SEEK_CUR);
    }

    return got_data;
}

// --- Background playback task ---
// Runs in its own FreeRTOS task, continuously reads from the file and
// writes to the I2S DMA. The blocking i2s_channel_write call inside
// audio_stream_write naturally throttles to the playback rate.

static void playback_task(void) {
    FILE *f = fopen(g_play_path, "rb");
    if (!f) {
        strcpy(playback_error, "Cannot open file");
        playback_done = true;
        return;
    }

    uint32_t data_offset;

    if (!parse_wav_header(f, &data_offset, &wav_data_size, &wav_channels, &wav_sample_rate, &wav_bits_per_sample)) {
        fclose(f);
        strcpy(playback_error, "Invalid WAV");
        playback_done = true;
        return;
    }

    if (wav_sample_rate != 44100) {
        fclose(f);
        snprintf(playback_error, sizeof(playback_error), "Need 44100Hz, got %ld", (long)wav_sample_rate);
        playback_done = true;
        return;
    }

    fseek(f, data_offset, SEEK_SET);

    if (!audio_stream_start()) {
        fclose(f);
        strcpy(playback_error, "Audio busy");
        playback_done = true;
        return;
    }

    int16_t *buf = malloc(CHUNK_FRAMES * 2 * sizeof(int16_t));
    if (!buf) {
        audio_stream_stop();
        fclose(f);
        strcpy(playback_error, "Out of memory");
        playback_done = true;
        return;
    }

    uint32_t pos = 0;

    while (pos < wav_data_size && !playback_done) {
        uint32_t bytes_left = wav_data_size - pos;
        uint32_t frames_to_read = CHUNK_FRAMES;
        uint32_t bytes_to_read = frames_to_read * wav_channels * 2;
        if (bytes_to_read > bytes_left) {
            bytes_to_read = bytes_left;
            frames_to_read = bytes_to_read / (wav_channels * 2);
        }

        size_t bytes_read = fread(buf, 1, bytes_to_read, f);
        if (bytes_read == 0) break;

        size_t frames_read = bytes_read / (wav_channels * 2);

        if (wav_channels == 1) {
            for (int i = (int)frames_read - 1; i >= 0; i--) {
                buf[i * 2]     = buf[i];
                buf[i * 2 + 1] = buf[i];
            }
        }

        audio_stream_write(buf, frames_read);
        pos += bytes_read;
        wav_position = pos;
    }

    free(buf);
    audio_stream_stop();
    fclose(f);
    playback_done = true;
}

// --- File browser ---

static bool has_wav_ext(const char *name) {
    int len = (int)strlen(name);
    if (len < 5) return false;
    const char *ext = name + len - 4;
    return (ext[0] == '.' && (ext[1] == 'w' || ext[1] == 'W') &&
            (ext[2] == 'a' || ext[2] == 'A') &&
            (ext[3] == 'v' || ext[3] == 'V'));
}

static int scan_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < MAX_FILES) {
        if (ent->d_name[0] == '.') continue;

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            snprintf(file_names[count], MAX_PATH, "[%s]", ent->d_name);
        } else if (has_wav_ext(ent->d_name)) {
            snprintf(file_names[count], MAX_PATH, "  %s", ent->d_name);
        } else {
            continue;
        }

        strcpy(file_paths[count], full);
        file_items[count] = file_names[count];
        count++;
    }
    closedir(d);

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(file_names[i], file_names[j]) > 0) {
                char tmp_name[MAX_PATH], tmp_path[MAX_PATH];
                strcpy(tmp_name, file_names[i]);
                strcpy(tmp_path, file_paths[i]);
                strcpy(file_names[i], file_names[j]);
                strcpy(file_paths[i], file_paths[j]);
                strcpy(file_names[j], tmp_name);
                strcpy(file_paths[j], tmp_path);
            }
        }
    }

    for (int i = 0; i < count; i++)
        file_items[i] = file_names[i];

    return count;
}

static void refresh_browser(void) {
    file_count = scan_files(cwd);
    if (file_list)
        ui2_list_set_items(file_list, file_items, file_count);
}

// --- Playback control ---

static void start_playback(const char *path) {
    wav_position = 0;
    playback_error[0] = '\0';
    playback_done = false;
    strncpy(g_play_path, path, MAX_PATH - 1);
    g_play_path[MAX_PATH - 1] = '\0';

    const char *slash = strrchr(path, '/');
    strncpy(playing_name, slash ? slash + 1 : path, sizeof(playing_name) - 1);
    playing_name[sizeof(playing_name) - 1] = '\0';

    audio_set_volume(80);

    if (!os_start_task(playback_task, "wav_play", 16384, 5)) {
        strcpy(playback_error, "Cannot start task");
        playback_done = true;
    }

    state = STATE_PLAYING;
}

static void stop_playback(void) {
    playback_done = true;
    audio_stream_stop();
}

// --- Drawing ---

static void render(void);

static void truncate(const char *src, char *dst, int max) {
    int len = (int)strlen(src);
    if (len <= max) { strcpy(dst, src); return; }
    if (max < 3) { dst[0] = '\0'; return; }
    memcpy(dst, src, max - 3);
    strcpy(dst + max - 3, "...");
}

static void draw_playing(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    text_mode_print_at_attr_bg((cols - 8) / 2, 0, " Playing ",
                               TEXT_COLOR_BRIGHT_CYAN, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);

    char name_disp[64];
    truncate(playing_name, name_disp, cols - 4);
    text_mode_print_at_attr_bg(2, 2, name_disp,
                               TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);

    char info[64];
    snprintf(info, sizeof(info), "%d-bit %s %ldHz",
             wav_bits_per_sample,
             wav_channels == 2 ? "stereo" : "mono",
             (long)wav_sample_rate);
    text_mode_print_at_attr_bg(2, 3, info,
                               TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);

    int bar_row = 5;
    int bar_width = cols - 4;
    if (bar_width > 50) bar_width = 50;
    int bar_x = (cols - bar_width) / 2;

    for (int i = 0; i < bar_width; i++)
        text_mode_print_at_attr_bg(bar_x + i, bar_row, "=",
                                   TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);

    float progress = wav_data_size > 0 ? (float)wav_position / wav_data_size : 0;
    if (progress > 1.0f) progress = 1.0f;
    int filled = (int)(progress * bar_width);
    for (int i = 0; i < filled; i++)
        text_mode_print_at_attr_bg(bar_x + i, bar_row, "=",
                                   TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, TEXT_ATTR_BOLD);

    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", (int)(progress * 100));
    text_mode_print_at_attr_bg((cols - (int)strlen(pct)) / 2, bar_row + 1, pct,
                               TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);

    uint32_t total_secs = wav_data_size / (wav_channels * 2 * wav_sample_rate);
    uint32_t cur_secs = wav_position / (wav_channels * 2 * wav_sample_rate);
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d",
             cur_secs / 60, cur_secs % 60, total_secs / 60, total_secs % 60);
    text_mode_print_at_attr_bg((cols - (int)strlen(time_buf)) / 2, bar_row + 2, time_buf,
                               TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);

    if (toolbar)
        UI2_WIDGET(toolbar)->vtable->draw(UI2_WIDGET(toolbar));
}

static void draw_browser(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    if (file_list)
        UI2_WIDGET(file_list)->vtable->draw(UI2_WIDGET(file_list));
    if (toolbar)
        UI2_WIDGET(toolbar)->vtable->draw(UI2_WIDGET(toolbar));

    char cwd_disp[MAX_PATH];
    truncate(cwd, cwd_disp, cols - 1);
    text_mode_print_at_attr_bg(0, rows - 1, cwd_disp,
                               TEXT_COLOR_BRIGHT_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
}

static void render(void) {
    switch (state) {
        case STATE_BROWSE:
        case STATE_FINISHED:
            draw_browser();
            break;
        case STATE_PLAYING:
            draw_playing();
            break;
    }
    text_mode_flush();
}

static void on_file_selected(int index, void *user_data) {
    (void)user_data;
}

static void play_selected(void) {
    int sel = file_list ? ui2_list_get_selection(file_list) : -1;
    if (sel < 0 || sel >= file_count) return;

    struct stat st;
    if (stat(file_paths[sel], &st) != 0) return;

    if (S_ISDIR(st.st_mode)) {
        strncpy(cwd, file_paths[sel], MAX_PATH - 1);
        cwd[MAX_PATH - 1] = '\0';
        refresh_browser();
    } else {
        start_playback(file_paths[sel]);
    }
}

// --- Toolbar callbacks ---

static void on_up(ui2_button_t *button, void *user_data) {
    (void)button; (void)user_data;
    if (state == STATE_BROWSE && file_list)
        UI2_WIDGET(file_list)->vtable->handle_key(UI2_WIDGET(file_list), 'w');
}

static void on_down(ui2_button_t *button, void *user_data) {
    (void)button; (void)user_data;
    if (state == STATE_BROWSE && file_list)
        UI2_WIDGET(file_list)->vtable->handle_key(UI2_WIDGET(file_list), 's');
}

static void on_select(ui2_button_t *button, void *user_data) {
    (void)button; (void)user_data;
    if (state == STATE_BROWSE)
        play_selected();
}

static void on_back(ui2_button_t *button, void *user_data) {
    (void)button; (void)user_data;
    if (state == STATE_PLAYING) {
        stop_playback();
        state = STATE_BROWSE;
    } else {
        char *slash = strrchr(cwd, '/');
        if (slash && slash != cwd) {
            *slash = '\0';
        } else {
            os_exit();
        }
    }
    refresh_browser();
}

static void on_exit_app(ui2_button_t *button, void *user_data) {
    (void)button; (void)user_data;
    stop_playback();
    os_exit();
}

// --- App lifecycle ---

void app_init(app_context_t *ctx) {
    if (!text_mode_init()) return;

    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH | EVENT_TIMER;
    ctx->timer_interval_ms = 100;

    state = STATE_BROWSE;

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    file_list = ui2_list_create(0, 0, cols, rows - 4);
    ui2_list_set_title(file_list, "WAV Files");
    ui2_list_set_colors(file_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                        TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
    ui2_list_set_border(file_list, true);
    ui2_list_set_scrollbar_width(file_list, 1);
    ui2_list_set_callbacks(file_list, on_file_selected, NULL, NULL);

    ui2_toolbar_item_t tb_items[] = {
        {ICON_ARROW_UP,   on_up,       NULL},
        {ICON_ARROW_DOWN, on_down,     NULL},
        {ICON_CHECK,      on_select,   NULL},
        {ICON_ARROW_LEFT, on_back,     NULL},
        {ICON_X,          on_exit_app, NULL},
    };
    toolbar = ui2_toolbar_create(0, rows - 4, cols, 3, tb_items, 5);

    refresh_browser();
    render();
}

void app_checkpoint(app_context_t *ctx) {
}

void app_close(app_context_t *ctx) {
    stop_playback();
    if (file_list) { UI2_WIDGET(file_list)->vtable->destroy(UI2_WIDGET(file_list)); file_list = NULL; }
    if (toolbar) { UI2_WIDGET(toolbar)->vtable->destroy(UI2_WIDGET(toolbar)); toolbar = NULL; }
    text_mode_clear(TEXT_COLOR_BLACK);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        if (event->keyboard.modifiers & MODIFIER_CTRL) return;

        switch (state) {
            case STATE_BROWSE:
                if (key == 27 || key == 'a' || key == 'A') {
                    on_back(NULL, NULL);
                    render();
                } else if (key == 'q' || key == 'Q') {
                    os_exit();
                } else if (key == '\n' || key == '\r') {
                    play_selected();
                    render();
                } else if (file_list) {
                    if (UI2_WIDGET(file_list)->vtable->handle_key(UI2_WIDGET(file_list), key))
                        render();
                }
                break;
            case STATE_PLAYING:
            case STATE_FINISHED:
                if (key == 27 || key == 'a' || key == 'A' || key == 'q' || key == 'Q') {
                    stop_playback();
                    state = STATE_BROWSE;
                    refresh_browser();
                    render();
                }
                break;
        }
    } else if (event->type == EVENT_TOUCH && event->touch.pressed) {
        int cw = text_mode_get_char_width();
        int ch = text_mode_get_char_height();
        int x_col = event->touch.x / cw;
        int y_col = event->touch.y / ch;

        if (toolbar && UI2_WIDGET(toolbar)->vtable->handle_touch(UI2_WIDGET(toolbar), x_col, y_col, true)) {
            render();
            return;
        }

        if (state == STATE_BROWSE && file_list) {
            UI2_WIDGET(file_list)->vtable->handle_touch(UI2_WIDGET(file_list), x_col, y_col, true);
            render();
        }
    } else if (event->type == EVENT_TIMER) {
        if (state == STATE_PLAYING) {
            if (playback_done) {
                state = STATE_BROWSE;
                refresh_browser();
            }
            render();
        }
    }
}
