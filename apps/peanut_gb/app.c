#define ENABLE_SOUND 0
#define ENABLE_LCD 1

#include "peanut_gb.h"

#include "os_core.h"
#include "hardware.h"
#include "text_mode.h"
#include "ui.h"
#include "ui_list.h"
#include "ui_toolbar.h"
#include "ui_button.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_timer.h"

static const char *TAG = "peanut_gb";

#define GB_SCREEN_X ((display_get_width() - 160) / 2)
#define GB_SCREEN_Y ((display_get_height() - 144) / 2)
#define GB_WIDTH 160
#define GB_HEIGHT 144

#define MAX_ROMS 128
#define ROMS_DIR "/sdcard/roms"

enum app_mode_e { MODE_ROM_LIST, MODE_PLAYING };

static enum app_mode_e app_mode = MODE_ROM_LIST;

static struct gb_s *gb = NULL;

static const uint8_t *rom_data = NULL;
static size_t rom_size = 0;

static uint8_t *rom_bank0 = NULL;

static uint8_t *sram_data = NULL;
static size_t sram_size = 0;
static bool sram_dirty = false;

static void *gb_sprite = NULL;
static uint8_t joypad_state = 0xFF;

#define GB_FPS 60
#define GB_FRAME_US (1000000 / GB_FPS)
static int64_t last_frame_time = 0;

typedef struct {
    char **names;
    char **paths;
    int count;
    int selected;
} rom_list_t;

static rom_list_t *rom_list_data = NULL;
static ui_list_widget_t *rom_list_widget = NULL;
static ui_toolbar_t *rom_toolbar = NULL;

static uint8_t gb_rom_read_cb(struct gb_s *gb_ctx, const uint_fast32_t addr) {
    if (addr >= rom_size) return 0xFF;
    if (addr < ROM_BANK_SIZE) return rom_bank0[addr];
    return rom_data[addr];
}

static uint8_t gb_cart_ram_read_cb(struct gb_s *gb_ctx, const uint_fast32_t addr) {
    if (!sram_data || addr >= sram_size) return 0xFF;
    return sram_data[addr];
}

static void gb_cart_ram_write_cb(struct gb_s *gb_ctx, const uint_fast32_t addr,
                                  const uint8_t val) {
    if (!sram_data || addr >= sram_size) return;
    sram_data[addr] = val;
    sram_dirty = true;
}

static void gb_error_cb(struct gb_s *gb_ctx, const enum gb_error_e err,
                         const uint16_t addr) {
    os_log(TAG, "GB error %d at 0x%04X", err, addr);
}

static void gb_lcd_draw_line_cb(struct gb_s *gb_ctx, const uint8_t *pixels,
                                 const uint_fast8_t line) {
    if (!gb_sprite || line >= GB_HEIGHT) return;
    sprite_write_row(gb_sprite, line, pixels, GB_WIDTH);
}

static char rom_path[270];

static void save_sram(void) {
    if (!sram_dirty || !sram_data || sram_size == 0) return;

    char sram_path[270];
    snprintf(sram_path, sizeof(sram_path), "%s.sav", rom_path);
    FILE *f = fopen(sram_path, "wb");
    if (!f) {
        os_log(TAG, "Failed to save SRAM: %s", sram_path);
        return;
    }
    size_t written = fwrite(sram_data, 1, sram_size, f);
    fclose(f);
    if (written != sram_size) {
        os_log(TAG, "SRAM write short: %d/%d", written, sram_size);
    } else {
        os_log(TAG, "SRAM saved: %d bytes", sram_size);
    }
    sram_dirty = false;
}

#define STATE_MAGIC 0x47534E50
#define STATE_VERSION 1

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
} state_header_t;

static void save_state(void) {
    if (!gb) return;

    char path[270];
    snprintf(path, sizeof(path), "%s.state", rom_path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        os_log(TAG, "Failed to save state: %s", path);
        return;
    }

    state_header_t header = {
        .magic = STATE_MAGIC,
        .version = STATE_VERSION,
        .struct_size = sizeof(struct gb_s)
    };
    fwrite(&header, sizeof(header), 1, f);
    fwrite(gb, sizeof(struct gb_s), 1, f);
    if (sram_data && sram_size > 0) {
        fwrite(sram_data, 1, sram_size, f);
    }
    fclose(f);
    os_log(TAG, "State saved: %d bytes", (int)sizeof(struct gb_s));
}

static bool load_state(void) {
    if (!gb) return false;

    char path[270];
    snprintf(path, sizeof(path), "%s.state", rom_path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        os_log(TAG, "No state file: %s", path);
        return false;
    }

    state_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1 ||
        header.magic != STATE_MAGIC ||
        header.version != STATE_VERSION ||
        header.struct_size != sizeof(struct gb_s)) {
        os_log(TAG, "State file invalid or size mismatch (file=%d, current=%d)",
               header.struct_size, (int)sizeof(struct gb_s));
        fclose(f);
        return false;
    }

    struct gb_s saved_ptrs;
    saved_ptrs.gb_rom_read = gb->gb_rom_read;
    saved_ptrs.gb_cart_ram_read = gb->gb_cart_ram_read;
    saved_ptrs.gb_cart_ram_write = gb->gb_cart_ram_write;
    saved_ptrs.gb_error = gb->gb_error;
    saved_ptrs.gb_serial_tx = gb->gb_serial_tx;
    saved_ptrs.gb_serial_rx = gb->gb_serial_rx;
    saved_ptrs.gb_bootrom_read = gb->gb_bootrom_read;
    saved_ptrs.display.lcd_draw_line = gb->display.lcd_draw_line;
    saved_ptrs.direct.priv = gb->direct.priv;

    if (fread(gb, sizeof(struct gb_s), 1, f) != 1) {
        os_log(TAG, "Failed to read state data");
        fclose(f);
        return false;
    }

    gb->gb_rom_read = saved_ptrs.gb_rom_read;
    gb->gb_cart_ram_read = saved_ptrs.gb_cart_ram_read;
    gb->gb_cart_ram_write = saved_ptrs.gb_cart_ram_write;
    gb->gb_error = saved_ptrs.gb_error;
    gb->gb_serial_tx = saved_ptrs.gb_serial_tx;
    gb->gb_serial_rx = saved_ptrs.gb_serial_rx;
    gb->gb_bootrom_read = saved_ptrs.gb_bootrom_read;
    gb->display.lcd_draw_line = saved_ptrs.display.lcd_draw_line;
    gb->direct.priv = saved_ptrs.direct.priv;
    gb->direct.joypad = joypad_state;

    if (sram_data && sram_size > 0) {
        fread(sram_data, 1, sram_size, f);
        sram_dirty = true;
    }

    fclose(f);
    os_log(TAG, "State loaded");
    return true;
}

static void cleanup_emulator(void) {
    save_sram();
    if (gb) { free(gb); gb = NULL; }
    if (rom_bank0) { free(rom_bank0); rom_bank0 = NULL; }
    if (sram_data) { free(sram_data); sram_data = NULL; }
    sram_size = 0;
    sram_dirty = false;
    if (gb_sprite) { sprite_destroy(gb_sprite); gb_sprite = NULL; }
    if (rom_data) { flash_rom_unload(); rom_data = NULL; }
    rom_size = 0;
}

static void free_rom_list_data(void) {
    if (!rom_list_data) return;
    if (rom_list_data->names) {
        for (int i = 0; i < rom_list_data->count; i++) {
            free(rom_list_data->names[i]);
        }
        free(rom_list_data->names);
    }
    if (rom_list_data->paths) {
        for (int i = 0; i < rom_list_data->count; i++) {
            free(rom_list_data->paths[i]);
        }
        free(rom_list_data->paths);
    }
    free(rom_list_data);
    rom_list_data = NULL;
}

static void cleanup_widgets(void) {
    if (rom_list_widget) { ui_list_destroy(rom_list_widget); rom_list_widget = NULL; }
    if (rom_toolbar) { ui_toolbar_destroy(rom_toolbar); rom_toolbar = NULL; }
}

static bool has_gb_extension(const char *name) {
    size_t len = strlen(name);
    if (len < 4) return false;
    if (strcmp(name + len - 3, ".gb") == 0) return true;
    if (len >= 5 && strcmp(name + len - 4, ".gbc") == 0) return true;
    return false;
}

static rom_list_t *scan_roms(void) {
    rom_list_t *list = calloc(1, sizeof(rom_list_t));
    if (!list) {
        os_log(TAG, "Failed to allocate rom_list_t");
        return NULL;
    }

    DIR *dir = opendir(ROMS_DIR);
    if (!dir) {
        os_log(TAG, "Creating ROMs directory: %s", ROMS_DIR);
        mkdir(ROMS_DIR, 0777);
        return list;
    }

    char *tmp_names[MAX_ROMS];
    char *tmp_paths[MAX_ROMS];
    int count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < MAX_ROMS) {
        if (entry->d_name[0] == '.') continue;
        if (!has_gb_extension(entry->d_name)) continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", ROMS_DIR, entry->d_name);

        tmp_names[count] = malloc(strlen(entry->d_name) + 1);
        tmp_paths[count] = malloc(strlen(path) + 1);
        if (tmp_names[count] && tmp_paths[count]) {
            strcpy(tmp_names[count], entry->d_name);
            strcpy(tmp_paths[count], path);
            char *dot = strrchr(tmp_names[count], '.');
            if (dot) *dot = '\0';
            count++;
        } else {
            os_log(TAG, "Failed to allocate ROM entry: %s", entry->d_name);
            free(tmp_names[count]);
            free(tmp_paths[count]);
        }
    }
    closedir(dir);

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(tmp_names[i], tmp_names[j]) > 0) {
                char *tn = tmp_names[i]; tmp_names[i] = tmp_names[j]; tmp_names[j] = tn;
                char *tp = tmp_paths[i]; tmp_paths[i] = tmp_paths[j]; tmp_paths[j] = tp;
            }
        }
    }

    list->names = calloc(count, sizeof(char *));
    list->paths = calloc(count, sizeof(char *));
    if ((count > 0) && (!list->names || !list->paths)) {
        os_log(TAG, "Failed to allocate ROM list arrays");
        for (int i = 0; i < count; i++) { free(tmp_names[i]); free(tmp_paths[i]); }
        free(list->names);
        free(list->paths);
        list->names = NULL;
        list->paths = NULL;
        list->count = 0;
        return list;
    }

    list->count = count;
    list->selected = 0;
    if (count > 0) {
        memcpy(list->names, tmp_names, count * sizeof(char *));
        memcpy(list->paths, tmp_paths, count * sizeof(char *));
    }

    os_log(TAG, "Found %d ROMs", count);
    return list;
}

static void open_selected_rom(void);
static void toolbar_up_click(ui_button_t *button, void *user_data);
static void toolbar_down_click(ui_button_t *button, void *user_data);
static void toolbar_open_click(ui_button_t *button, void *user_data);
static void toolbar_exit_click(ui_button_t *button, void *user_data);

static void on_rom_selected(ui_list_widget_t *list, int item_index, void *user_data) {
    (void)list;
    (void)user_data;
    if (rom_list_data) rom_list_data->selected = item_index;
    open_selected_rom();
}

static void on_rom_selection_changed(ui_list_widget_t *list, int new_selection, void *user_data) {
    (void)list;
    (void)user_data;
    if (rom_list_data) rom_list_data->selected = new_selection;
}

static void toolbar_up_click(ui_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (rom_list_widget) {
        ui_list_handle_key(rom_list_widget, 'u');
        if (rom_list_data) rom_list_data->selected = rom_list_widget->selected;
        ui_list_draw(rom_list_widget);
        text_mode_flush();
    }
}

static void toolbar_down_click(ui_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (rom_list_widget) {
        ui_list_handle_key(rom_list_widget, 'd');
        if (rom_list_data) rom_list_data->selected = rom_list_widget->selected;
        ui_list_draw(rom_list_widget);
        text_mode_flush();
    }
}

static void toolbar_open_click(ui_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    open_selected_rom();
}

static void toolbar_exit_click(ui_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    os_load_app("launcher");
}

static void draw_rom_list(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_height = rows - 5;

    ui_clear();
    cleanup_widgets();

    rom_list_widget = ui_list_create(1, 1, cols - 2, list_height);
    if (!rom_list_widget) {
        os_log(TAG, "Failed to create ROM list widget");
        return;
    }
    ui_list_set_title(rom_list_widget, "Select a ROM");
    ui_list_set_colors(rom_list_widget, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                       TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
    ui_list_set_border(rom_list_widget, true);
    ui_list_set_scrollbar(rom_list_widget, true);
    ui_list_set_callbacks(rom_list_widget, on_rom_selection_changed, on_rom_selected, NULL);

    if (rom_list_data && rom_list_data->count > 0) {
        ui_list_set_items(rom_list_widget, (const char **)rom_list_data->names,
                          rom_list_data->count);
        ui_list_set_selection(rom_list_widget, rom_list_data->selected);
        ui_list_draw(rom_list_widget);
    } else {
        ui_label(2, 2, "No ROMs found", TEXT_COLOR_YELLOW);
        ui_label(2, 4, "Place .gb files in", TEXT_COLOR_BRIGHT_BLACK);
        ui_label(2, 5, ROMS_DIR, TEXT_COLOR_BRIGHT_BLACK);
    }

    int button_row = rows - 3;
    const char *labels[] = {"Up", "Dn", "Open", "Exit"};
    rom_toolbar = ui_toolbar_create(button_row, 3, 4, labels);
    if (rom_toolbar) {
        ui_button_set_callback(ui_toolbar_get_button(rom_toolbar, 0), toolbar_up_click, NULL);
        ui_button_set_callback(ui_toolbar_get_button(rom_toolbar, 1), toolbar_down_click, NULL);
        ui_button_set_callback(ui_toolbar_get_button(rom_toolbar, 2), toolbar_open_click, NULL);
        ui_button_set_callback(ui_toolbar_get_button(rom_toolbar, 3), toolbar_exit_click, NULL);
        ui_toolbar_draw(rom_toolbar);
    } else {
        os_log(TAG, "Failed to create toolbar");
    }
}

static void show_rom_list(void) {
    app_mode = MODE_ROM_LIST;
    joypad_state = 0xFF;
    cleanup_emulator();
    free_rom_list_data();
    rom_list_data = scan_roms();
    text_mode_init();
    draw_rom_list();
}

static bool start_emulator(const char *path) {
    strncpy(rom_path, path, sizeof(rom_path) - 1);
    rom_path[sizeof(rom_path) - 1] = '\0';

    cleanup_widgets();
    free_rom_list_data();

    rom_data = flash_rom_load(rom_path, &rom_size);
    if (!rom_data) {
        os_log(TAG, "Failed to load ROM to flash: %s", path);
        return false;
    }

    if (rom_size < 0x150) {
        os_log(TAG, "ROM too small: %d bytes", rom_size);
        cleanup_emulator();
        return false;
    }

    gb = malloc(sizeof(struct gb_s));
    rom_bank0 = malloc(ROM_BANK_SIZE);
    if (!gb || !rom_bank0) {
        os_log(TAG, "Failed to allocate: gb=%p bank0=%p", gb, rom_bank0);
        cleanup_emulator();
        return false;
    }
    memcpy(rom_bank0, rom_data, ROM_BANK_SIZE);

    gb_sprite = display_create_sprite(GB_WIDTH, GB_HEIGHT, 2);
    if (!gb_sprite) {
        os_log(TAG, "Failed to create display sprite");
        cleanup_emulator();
        return false;
    }
    sprite_set_palette_color(gb_sprite, 0, 0xFFFF);
    sprite_set_palette_color(gb_sprite, 1, 0xAD55);
    sprite_set_palette_color(gb_sprite, 2, 0x52AA);
    sprite_set_palette_color(gb_sprite, 3, 0x0000);

    os_log(TAG, "Allocated: gb=%d bank0=%d ROM=%d", (int)sizeof(struct gb_s), ROM_BANK_SIZE, rom_size);

    enum gb_init_error_e ret = gb_init(gb, gb_rom_read_cb, gb_cart_ram_read_cb,
                                        gb_cart_ram_write_cb, gb_error_cb, NULL);
    if (ret != GB_INIT_NO_ERROR) {
        os_log(TAG, "gb_init failed: error %d", ret);
        cleanup_emulator();
        return false;
    }

    size_t save_size = 0;
    gb_get_save_size_s(gb, &save_size);
    if (save_size > 0) {
        sram_data = malloc(save_size);
        if (!sram_data) {
            os_log(TAG, "Failed to allocate SRAM: %d bytes", save_size);
            cleanup_emulator();
            return false;
        }
        sram_size = save_size;
        memset(sram_data, 0, save_size);

        char sram_path[270];
        snprintf(sram_path, sizeof(sram_path), "%s.sav", rom_path);
        FILE *sf = fopen(sram_path, "rb");
        if (sf) {
            size_t loaded = fread(sram_data, 1, save_size, sf);
            fclose(sf);
            os_log(TAG, "Loaded SRAM: %d/%d bytes", loaded, save_size);
        } else {
            os_log(TAG, "No SRAM file (new game)");
        }
    }

    os_time_status_t ts;
    if (os_get_time_status(&ts) && ts.synchronized) {
        struct tm tm_val;
        memset(&tm_val, 0, sizeof(tm_val));
        tm_val.tm_year = ts.year - 1900;
        tm_val.tm_mon = ts.month - 1;
        tm_val.tm_mday = ts.day;
        tm_val.tm_hour = ts.hour;
        tm_val.tm_min = ts.minute;
        tm_val.tm_sec = ts.second;
        gb_set_rtc(gb, &tm_val);
    }

    gb_init_lcd(gb, gb_lcd_draw_line_cb);

    char title[17];
    gb_get_rom_name(gb, title);
    title[16] = '\0';
    os_log(TAG, "Loaded: \"%s\" (%d bytes, save=%d)", title, rom_size, save_size);

    app_mode = MODE_PLAYING;
    sprite_set_active(gb_sprite);
    last_frame_time = esp_timer_get_time();
    display_clear(0x0000);
    return true;
}

static void open_selected_rom(void) {
    if (!rom_list_data || rom_list_data->count <= 0 ||
        rom_list_data->selected < 0 || rom_list_data->selected >= rom_list_data->count) {
        os_log(TAG, "No ROM selected");
        return;
    }
    os_log(TAG, "Opening: %s", rom_list_data->paths[rom_list_data->selected]);
    if (!start_emulator(rom_list_data->paths[rom_list_data->selected])) {
        os_log(TAG, "Failed to start emulator, returning to ROM list");
        show_rom_list();
    }
}

static inline uint32_t get_ccount(void) {
    uint32_t ccount;
    __asm__ volatile("rsr %0, ccount" : "=r"(ccount));
    return ccount;
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER | EVENT_TOUCH;
    ctx->timer_interval_ms = 16;

    char startup_file[256];
    size_t file_len = os_consume_startup_file(startup_file, sizeof(startup_file) - 1);
    if (file_len > 0) {
        startup_file[file_len] = '\0';
        os_log(TAG, "Startup file: %s", startup_file);
        if (start_emulator(startup_file)) return;
        os_log(TAG, "Failed to start from file, showing ROM list");
    }

    show_rom_list();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (app_mode == MODE_PLAYING) {
        if (event->type == EVENT_TIMER) {
            if (!gb || !gb_sprite) return;
            gb->direct.joypad = joypad_state;

            int64_t now = esp_timer_get_time();
            if (last_frame_time == 0) last_frame_time = now;
            int64_t elapsed = now - last_frame_time;
            int frames_to_run = (int)(elapsed / GB_FRAME_US);
            if (frames_to_run < 1) frames_to_run = 1;
            if (frames_to_run > 5) frames_to_run = 5;

            uint32_t c0 = get_ccount();
            for (int i = 0; i < frames_to_run; i++) {
                gb_run_frame(gb);
            }
            uint32_t c1 = get_ccount();
            sprite_push(gb_sprite, GB_SCREEN_X, GB_SCREEN_Y);
            uint32_t c2 = get_ccount();
            last_frame_time += frames_to_run * GB_FRAME_US;

            static int frame_count = 0;
            frame_count++;
            if (frame_count % 60 == 0) {
                os_log(TAG, "perf: emulate=%u push=%u total=%u frames=%d",
                       c1 - c0, c2 - c1, c2 - c0, frames_to_run);
            }
            return;
        }

        if (event->type == EVENT_KEYBOARD) {
            uint8_t button = 0;
            switch (event->keyboard.key) {
                case 'w': case 'W': button = JOYPAD_UP; break;
                case 's': case 'S': button = JOYPAD_DOWN; break;
                case 'a': case 'A': button = JOYPAD_LEFT; break;
                case 'd': case 'D': button = JOYPAD_RIGHT; break;
                case 'l': case 'L': button = JOYPAD_A; break;
                case 'm': case 'M': button = JOYPAD_B; break;
                case 'o': case 'O': button = JOYPAD_SELECT; break;
                case 'p': case 'P': button = JOYPAD_START; break;
                default: break;
            }

            if (button != 0) {
                if (event->keyboard.pressed) {
                    joypad_state &= ~button;
                } else {
                    joypad_state |= button;
                }
                gb->direct.joypad = joypad_state;
                gb_run_frame(gb);
                sprite_push(gb_sprite, GB_SCREEN_X, GB_SCREEN_Y);
                return;
            }

            if (event->keyboard.pressed && event->keyboard.key == 27) {
                show_rom_list();
            }

            if (event->keyboard.pressed) {
                if (event->keyboard.key == 'k' || event->keyboard.key == 'K') {
                    save_state();
                }
                if (event->keyboard.key == 'j' || event->keyboard.key == 'J') {
                    if (load_state()) {
                        gb_run_frame(gb);
                        sprite_push(gb_sprite, GB_SCREEN_X, GB_SCREEN_Y);
                    }
                }
            }
        }
        return;
    }

    if (app_mode == MODE_ROM_LIST) {
        if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
            if (rom_list_widget && ui_list_handle_key(rom_list_widget, event->keyboard.key)) {
                if (app_mode == MODE_ROM_LIST && rom_list_widget) {
                    if (rom_list_data) rom_list_data->selected = rom_list_widget->selected;
                    ui_list_draw(rom_list_widget);
                    text_mode_flush();
                }
                return;
            }

            if (event->keyboard.key == 27) {
                os_load_app("launcher");
            }
            return;
        }

        if (event->type == EVENT_TOUCH) {
            if (rom_toolbar && ui_toolbar_handle_touch(rom_toolbar, event)) {
                text_mode_flush();
                return;
            }
            if (rom_list_widget && ui_list_handle_touch(rom_list_widget, event)) {
                if (app_mode == MODE_ROM_LIST && rom_list_widget) {
                    if (rom_list_data) rom_list_data->selected = rom_list_widget->selected;
                    ui_list_draw(rom_list_widget);
                    text_mode_flush();
                }
                return;
            }
        }
    }
}

void app_checkpoint(app_context_t *ctx) {
    save_sram();
}

void app_close(app_context_t *ctx) {
    cleanup_widgets();
    free_rom_list_data();
    cleanup_emulator();
    display_clear(0x0000);
}
