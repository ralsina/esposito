#define ENABLE_SOUND 0
#define ENABLE_LCD 1

#include <stdint.h>
#include <stddef.h>

#include "walnut_cgb.h"

#include "os_core.h"
#include "hardware.h"
#include "text_mode.h"
#include "ui2.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_timer.h"

static const char *TAG = "peanut_gb";

#define GB_WIDTH 160
#define GB_HEIGHT 144

// Calculate best scaling factor based on screen size
static float SCALE_FACTOR = 1.0;
static int gb_screen_x = 0;
static int gb_screen_y = 0;

#define MAX_ROMS 128
#define ROMS_DIR "/sdcard/roms"

enum app_mode_e { MODE_ROM_LIST, MODE_PLAYING };

static enum app_mode_e app_mode = MODE_ROM_LIST;

static struct gb_s *gb = NULL;

static const uint8_t *rom_data = NULL;
static size_t rom_size = 0;

static uint8_t *rom_bank0 = NULL;

// SD card-based save game support (no RAM allocation)
static char sram_path[270] = {0};
static bool sram_enabled = false;

static void *display_sprite[2] = {NULL, NULL};  // Double buffering: two sprites (160x144 each)
static volatile int write_buffer_index = 0;     // Which buffer Core 0 is writing to
static volatile int read_buffer_index = 0;     // Which buffer Core 1 is reading/pushing
static volatile bool buffer_ready[2] = {false, false};  // Is each buffer ready to push?
static volatile int current_line = 0;           // Current line being written by Core 0
static volatile bool frame_complete = false;   // Frame complete flag
static volatile int rendered_frame_count = 0;  // Frame counter incremented by display task
static volatile int total_frame_count = 0;     // Total frames attempted by emulator
static volatile int skipped_frame_count = 0;    // Frames skipped by frame skip mechanism
static uint8_t joypad_state = 0xFF;

#define GB_FPS 60
#define GB_FRAME_US (1000000 / GB_FPS)
static int64_t last_frame_time = 0;
static int64_t fps_report_time = 0;  // Track time for FPS reporting

// Dual-core rendering
static os_task_handle_t *display_task_handle = NULL;
static os_semaphore_handle_t *frame_ready_semaphore = NULL;
static os_semaphore_handle_t *frame_done_semaphore = NULL;
static volatile bool display_task_running = false;

typedef struct {
    char **names;
    char **paths;
    int count;
    int selected;
} rom_list_t;

static rom_list_t *rom_list_data = NULL;
static ui2_screen_t *screen = NULL;
static ui2_list_t *rom_list = NULL;

// ROM read callback - 8-bit
static uint8_t gb_rom_read_cb(struct gb_s *gb_ctx, const uint_fast32_t addr) {
    if (addr >= rom_size) return 0xFF;
    if (addr < ROM_BANK_SIZE) return rom_bank0[addr];
    return rom_data[addr];
}

// ROM read callback - 16-bit with alignment check for ESP32
static uint16_t gb_rom_read16_cb(struct gb_s *gb_ctx, const uint_fast32_t addr) {
    const uint8_t *src;
    if (addr >= rom_size) return 0xFFFF;
    if (addr < ROM_BANK_SIZE) {
        src = rom_bank0 + addr;
    } else {
        src = rom_data + addr;
    }
    // Alignment check for ESP32 compatibility
    if ((uintptr_t)src & 1) {
        // Fallback to safe 8-bit reads when not aligned
        return ((uint16_t)src[0]) | ((uint16_t)src[1] << 8);
    }
    return *(uint16_t *)src;
}

// ROM read callback - 32-bit with alignment check for ESP32
static uint32_t gb_rom_read32_cb(struct gb_s *gb_ctx, const uint_fast32_t addr) {
    const uint8_t *src;
    if (addr >= rom_size) return 0xFFFFFFFF;
    if (addr < ROM_BANK_SIZE) {
        src = rom_bank0 + addr;
    } else {
        src = rom_data + addr;
    }
    // Alignment check: ESP32 flash/PSRAM require 32-bit alignment
    if ((uintptr_t)src & 3) {
        // Fallback to safe 8-bit reads when not aligned
        return ((uint32_t)src[0]) |
               ((uint32_t)src[1] << 8) |
               ((uint32_t)src[2] << 16) |
               ((uint32_t)src[3] << 24);
    }
    return *(uint32_t *)src;
}

static uint8_t gb_cart_ram_read_cb(struct gb_s *gb_ctx, const uint_fast32_t addr) {
    // SRAM reads from SD card file (currently returns 0xFF - no save support)
    return 0xFF;
}

static void gb_cart_ram_write_cb(struct gb_s *gb_ctx, const uint_fast32_t addr,
                                  const uint8_t val) {
    // SRAM writes to SD card file (currently noop - no save support)
    (void)addr;
    (void)val;
}

static void gb_error_cb(struct gb_s *gb_ctx, const enum gb_error_e err,
                         const uint16_t addr) {
    os_log(TAG, "GB error %d at 0x%04X", err, addr);
}

static void gb_lcd_draw_line_cb(struct gb_s *gb_ctx, const uint8_t *pixels,
                                 const uint_fast8_t line) {
    if (line >= GB_HEIGHT) return;

    // Check if this frame is being skipped (frame 0 of each pair when frame_skip is enabled)
    if (gb_ctx->direct.frame_skip && !gb_ctx->display.frame_skip_count) {
        if (line == 0) {
            skipped_frame_count++;
            os_log(TAG, "Frame SKIP (total skipped: %d/%d)", skipped_frame_count, total_frame_count);
        }
        return;  // Skip rendering this frame
    }

    // Count first line of each frame as a frame start
    if (line == 0) {
        total_frame_count++;
    }

    // Wait if the write buffer is still being pushed (Core 1 hasn't flipped yet)
    while (buffer_ready[write_buffer_index]) {
        os_semaphore_take(frame_done_semaphore, 1);
    }

    // Write scanline directly to sprite buffer (Core 0 - emulation only)
    void *sprite = display_sprite[write_buffer_index];
    for (int x = 0; x < GB_WIDTH; x++) {
        sprite_draw_pixel(sprite, x, line, pixels[x]);
    }

    current_line = line;
    frame_complete = (line == GB_HEIGHT - 1);

    // If frame complete, mark buffer as ready and signal display task
    if (frame_complete) {
        buffer_ready[write_buffer_index] = true;
        os_semaphore_give(frame_ready_semaphore);
    }
}

static void display_task(void *pvParameters) {
    (void)pvParameters;

    while (display_task_running) {
        // Wait for a complete frame to be ready
        if (os_semaphore_take(frame_ready_semaphore, -1)) {
            // Find which buffer is ready
            int ready_buffer = -1;
            for (int i = 0; i < 2; i++) {
                if (buffer_ready[i]) {
                    ready_buffer = i;
                    break;
                }
            }

            if (ready_buffer >= 0 && display_sprite[ready_buffer]) {
                // Set pivot to top-left corner before pushing
                sprite_set_pivot(display_sprite[ready_buffer], 0.0, 0.0);

                // Mark this sprite as active for screenshots
                sprite_set_active(display_sprite[ready_buffer]);

                // Push the complete frame with hardware scaling
                sprite_push_rotated_zoom(display_sprite[ready_buffer], gb_screen_x, gb_screen_y, 0.0, SCALE_FACTOR, SCALE_FACTOR);

                // Mark buffer as available for Core 0
                buffer_ready[ready_buffer] = false;

                // Signal Core 0 that it can continue rendering
                os_semaphore_give(frame_done_semaphore);

                rendered_frame_count++;
            }
        }
    }
}

static char rom_path[270];

// SRAM save support disabled - would use SD card storage
static void save_sram(void) {
    // TODO: Implement SD card-based save game support
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
    // SRAM save support disabled
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

    // SRAM load support disabled

    fclose(f);
    os_log(TAG, "State loaded");
    return true;
}

static void cleanup_emulator(void) {
    display_task_running = false;

    if (display_task_handle) {
        os_task_delete(display_task_handle);
        display_task_handle = NULL;
    }

    if (frame_ready_semaphore) {
        os_semaphore_delete(frame_ready_semaphore);
        frame_ready_semaphore = NULL;
    }

    if (frame_done_semaphore) {
        os_semaphore_delete(frame_done_semaphore);
        frame_done_semaphore = NULL;
    }

    save_sram();
    if (gb) { free(gb); gb = NULL; }
    if (rom_bank0) { free(rom_bank0); rom_bank0 = NULL; }
    // SRAM cleanup not needed (using SD card storage)
    for (int i = 0; i < 2; i++) {
        if (display_sprite[i]) {
            sprite_destroy(display_sprite[i]);
            display_sprite[i] = NULL;
        }
    }
    write_buffer_index = 0;
    read_buffer_index = 0;
    buffer_ready[0] = false;
    buffer_ready[1] = false;
    current_line = 0;
    frame_complete = false;
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
static void on_up_click(ui2_button_t *button, void *user_data);
static void on_down_click(ui2_button_t *button, void *user_data);
static void on_open_click(ui2_button_t *button, void *user_data);
static void on_exit_click(ui2_button_t *button, void *user_data);

static void on_rom_activated(int item_index, void *user_data) {
    (void)user_data;
    if (rom_list_data) rom_list_data->selected = item_index;
    open_selected_rom();
}

static void on_rom_selection_changed(int new_selection, void *user_data) {
    (void)user_data;
    if (rom_list_data) rom_list_data->selected = new_selection;
}

static void on_up_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (rom_list) {
        int sel = ui2_list_get_selection(rom_list);
        if (sel > 0) ui2_list_set_selection(rom_list, sel - 1);
    }
}

static void on_down_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    if (rom_list) {
        int sel = ui2_list_get_selection(rom_list);
        if (sel < rom_list->count - 1) ui2_list_set_selection(rom_list, sel + 1);
    }
}

static void on_open_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    open_selected_rom();
}

static void on_exit_click(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    os_load_app("launcher");
}

static void destroy_screen(void) {
    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
        rom_list = NULL;
    }
}

static void build_rom_list_screen(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_height = rows - 5;

    screen = ui2_screen_create();
    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_screen_set_root(screen, root);

    if (rom_list_data && rom_list_data->count > 0) {
        rom_list = ui2_list_create(1, 1, cols - 2, list_height);
        if (!rom_list) {
            os_log(TAG, "Failed to create ROM list widget");
            return;
        }
        ui2_list_set_title(rom_list, "Select a ROM");
        ui2_list_set_colors(rom_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                            TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui2_list_set_items(rom_list, (const char **)rom_list_data->names,
                           rom_list_data->count);
        ui2_list_set_selection(rom_list, rom_list_data->selected);
        ui2_list_set_callbacks(rom_list, on_rom_selection_changed, on_rom_activated, NULL);
        ui2_layout_add(root, UI2_WIDGET(rom_list));
        ui2_screen_focus_set(screen, UI2_WIDGET(rom_list));
    } else {
        ui2_label_t *no_roms = ui2_label_create(2, 2, "No ROMs found",
                                                 TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
        ui2_label_t *dir_msg = ui2_label_create(2, 4, "Place .gb files in",
                                                 TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
        ui2_label_t *dir_path = ui2_label_create(2, 5, ROMS_DIR,
                                                  TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(no_roms));
        ui2_layout_add(root, UI2_WIDGET(dir_msg));
        ui2_layout_add(root, UI2_WIDGET(dir_path));
    }

    ui2_layout_t *bar = ui2_layout_create(1, rows - 3, cols - 2, 3, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(bar, 2);
    ui2_layout_add(root, UI2_WIDGET(bar));

    ui2_button_t *up = ui2_button_create(0, 0, 6, 3, "Up");
    ui2_button_set_callback(up, on_up_click, NULL);
    ui2_layout_add(bar, UI2_WIDGET(up));

    ui2_button_t *dn = ui2_button_create(0, 0, 6, 3, "Dn");
    ui2_button_set_callback(dn, on_down_click, NULL);
    ui2_layout_add(bar, UI2_WIDGET(dn));

    ui2_button_t *open = ui2_button_create(0, 0, 8, 3, "Open");
    ui2_button_set_callback(open, on_open_click, NULL);
    ui2_layout_add(bar, UI2_WIDGET(open));

    ui2_button_t *exit = ui2_button_create(0, 0, 8, 3, "Exit");
    ui2_button_set_callback(exit, on_exit_click, NULL);
    ui2_layout_add(bar, UI2_WIDGET(exit));

    ui2_screen_render(screen);
}

static void show_rom_list(void) {
    app_mode = MODE_ROM_LIST;
    joypad_state = 0xFF;
    cleanup_emulator();
    free_rom_list_data();
    rom_list_data = scan_roms();
    text_mode_init();
    destroy_screen();
    build_rom_list_screen();
}

static bool start_emulator(const char *path) {
    strncpy(rom_path, path, sizeof(rom_path) - 1);
    rom_path[sizeof(rom_path) - 1] = '\0';

    destroy_screen();
    free_rom_list_data();
    text_mode_init();

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

    gb = calloc(1, sizeof(struct gb_s));
    rom_bank0 = calloc(1, ROM_BANK_SIZE);
    if (!gb || !rom_bank0) {
        os_log(TAG, "Failed to allocate: gb=%p bank0=%p", gb, rom_bank0);
        cleanup_emulator();
        return false;
    }
    memcpy(rom_bank0, rom_data, ROM_BANK_SIZE);

    // Calculate best scaling factor based on screen size
    // Use only clean scaling factors: 2.0, 1.5, or 1.0
    int screen_w = display_get_width();
    int screen_h = display_get_height();
    float scale_x = (float)screen_w / GB_WIDTH;
    float scale_y = (float)screen_h / GB_HEIGHT;
    float min_scale = (scale_x < scale_y) ? scale_x : scale_y;

    // Choose best clean scaling factor
    if (min_scale >= 2.0) {
        SCALE_FACTOR = 2.0;
    } else if (min_scale >= 1.5) {
        SCALE_FACTOR = 1.5;
    } else {
        SCALE_FACTOR = 1.0;
    }

    // Calculate position to center the scaled image
    int scaled_w = (int)(GB_WIDTH * SCALE_FACTOR);
    int scaled_h = (int)(GB_HEIGHT * SCALE_FACTOR);
    gb_screen_x = (screen_w - scaled_w) / 2;
    gb_screen_y = (screen_h - scaled_h) / 2;

    os_log(TAG, "Screen: %dx%d, GB: %dx%d, Scale: %.1f, Pos: (%d,%d), Size: %dx%d",
           screen_w, screen_h, GB_WIDTH, GB_HEIGHT, SCALE_FACTOR, gb_screen_x, gb_screen_y, scaled_w, scaled_h);

    // Create two sprites for double buffering
    // Keep 2-bit (4 color) sprites for memory efficiency - emulator outputs 2-bit indices
    for (int i = 0; i < 2; i++) {
        display_sprite[i] = display_create_sprite(GB_WIDTH, GB_HEIGHT, 2);
        if (!display_sprite[i]) {
            os_log(TAG, "Failed to create display sprite %d", i);
            cleanup_emulator();
            return false;
        }

        // Set up default grayscale palette (will be updated for Color games)
        for (int c = 0; c < 4; c++) {
            uint16_t color;
            switch (c) {
                case 0: color = 0xFFFF; break;  // White/lightest
                case 1: color = 0xAD55; break;  // Light gray
                case 2: color = 0x52AA; break;  // Dark gray
                case 3: color = 0x0000; break;  // Black/darkest
            }
            sprite_set_palette_color(display_sprite[i], c, color);
        }
    }

    // Set first sprite as active for screenshots
    sprite_set_active(display_sprite[0]);

    frame_ready_semaphore = os_semaphore_create();
    frame_done_semaphore = os_semaphore_create();
    if (!frame_ready_semaphore || !frame_done_semaphore) {
        os_log(TAG, "Failed to create semaphores");
        cleanup_emulator();
        return false;
    }

    display_task_running = true;
    write_buffer_index = 0;
    read_buffer_index = 0;
    buffer_ready[0] = false;
    buffer_ready[1] = false;
    current_line = 0;
    frame_complete = false;

    display_task_handle = os_task_create(display_task, "gb_display", 4096, 5, 1);
    if (!display_task_handle) {
        os_log(TAG, "Failed to create display task");
        cleanup_emulator();
        return false;
    }

    os_log(TAG, "Allocated: gb=%d bank0=%d ROM=%d", (int)sizeof(struct gb_s), ROM_BANK_SIZE, rom_size);

    enum gb_init_error_e ret = gb_init(gb, &gb_rom_read_cb, &gb_rom_read16_cb, &gb_rom_read32_cb,
                                        &gb_cart_ram_read_cb, &gb_cart_ram_write_cb, &gb_error_cb, NULL);
    if (ret != GB_INIT_NO_ERROR) {
        os_log(TAG, "gb_init failed: error %d", ret);
        cleanup_emulator();
        return false;
    }

    // SRAM support disabled - using SD card saves would avoid RAM usage
    // For now, games with save RAM will run but won't persist saves
    size_t save_size = 0;
    gb_get_save_size_s(gb, &save_size);
    if (save_size > 0) {
        os_log(TAG, "ROM requires %d bytes save RAM (save support disabled)", save_size);
        // TODO: Implement SD card-based save game support
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

    // Enable frame skipping to maintain correct game speed at lower rendering FPS
    // This allows the game logic to run at 60 FPS even if we can only render 30-37 FPS
    gb->direct.frame_skip = true;
    os_log(TAG, "Frame skipping ENABLED for correct game speed");

    char title[17];
    gb_get_rom_name(gb, title);
    title[16] = '\0';
    os_log(TAG, "Loaded: \"%s\" (%d bytes, save=%d)", title, rom_size, save_size);

    os_log(TAG, "Dual-core rendering enabled (Core 0: emulation, Core 1: display)");

    app_mode = MODE_PLAYING;
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

    os_set_cpu_freq_mhz(240);
    fps_report_time = esp_timer_get_time();

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
            if (!gb || !display_sprite) return;
            gb->direct.joypad = joypad_state;

            int64_t now = esp_timer_get_time();
            if (last_frame_time == 0) last_frame_time = now;
            int64_t elapsed = now - last_frame_time;
            int frames_to_run = (int)(elapsed / GB_FRAME_US);
            if (frames_to_run < 1) frames_to_run = 1;
            if (frames_to_run > 5) frames_to_run = 5;

            uint32_t c0 = get_ccount();
            for (int i = 0; i < frames_to_run; i++) {
                gb_run_frame_dualfetch(gb);
            }
            uint32_t c1 = get_ccount();

            last_frame_time += frames_to_run * GB_FRAME_US;

            // Report FPS every 10 seconds
            int64_t fps_elapsed = now - fps_report_time;
            if (fps_elapsed >= 10000000) {  // 10 seconds
                int frames = rendered_frame_count;
                rendered_frame_count = 0;
                if (frames > 0) {
                    float fps = frames / (fps_elapsed / 1000000.0f);
                    os_log(TAG, "FPS: %.1f", fps);
                }
                fps_report_time = now;
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
                gb_run_frame_dualfetch(gb);

                // Frame already handled by LCD callback line-by-line
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
                        gb_run_frame_dualfetch(gb);

                        // Frame already handled by LCD callback line-by-line
                    }
                }
            }
        }
        return;
    }

    if (app_mode == MODE_ROM_LIST) {
        if (event->type == EVENT_KEYBOARD) {
            if (event->keyboard.pressed) {
                if (ui2_screen_handle_event(screen, event)) {
                    ui2_screen_render(screen);
                    return;
                }
                if (event->keyboard.key == 27) {
                    os_load_app("launcher");
                }
            }
            return;
        }

        if (event->type == EVENT_TOUCH) {
            if (ui2_screen_handle_event(screen, event)) {
                ui2_screen_render(screen);
            }
            return;
        }
    }
}

void app_checkpoint(app_context_t *ctx) {
    save_sram();
}

void app_close(app_context_t *ctx) {
    destroy_screen();
    free_rom_list_data();
    cleanup_emulator();
    display_clear(0x0000);
}
