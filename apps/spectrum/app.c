#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

#include "os_core.h"
#include "graphics_mode.h"
#include "text_mode.h"
#include "audio.h"
#include "misc.h"
#include "ui2.h"
#include "ui2_toolbar.h"
#include "lucide_icons.h"

#include "spscr.h"
#include "spscr_p.h"
#include "spsound.h"
#include "spperif.h"
#include "sptiming.h"
#include "spkey_p.h"
#include "spconf.h"
#include "z80.h"
#include "loader.h"
#include "game.h"

#define MAX_FILES 128
#define ROMS_DIR "/sdcard/roms"
#define BUILTIN_BASIC_PATH "__builtin_basic__"
#define BUILTIN_MM_PATH   "__builtin_mm__"

enum app_mode_e { MODE_FILE_LIST, MODE_PLAYING };
static enum app_mode_e app_mode = MODE_FILE_LIST;

static uint8_t *fb_buffer = NULL;
static bool initialized = false;

int display_get_width(void);
int display_get_height(void);

typedef struct {
    char **names;
    char **paths;
    int count;
    int selected;
} file_list_t;

static file_list_t *file_list_data = NULL;
static ui2_screen_t *screen = NULL;
static ui2_list_t *file_list = NULL;

// Spectrum key positions
static int key_row(char c)
{
    switch (c) {
        case ' ': return 7; case '0': return 4; case '1': return 3;
        case '2': return 3; case '3': return 3; case '4': return 3;
        case '5': return 3; case '6': return 4; case '7': return 4;
        case '8': return 4; case '9': return 4;
        case 'a': return 1; case 'b': return 7; case 'c': return 0;
        case 'd': return 1; case 'e': return 2; case 'f': return 1;
        case 'g': return 1; case 'h': return 6; case 'i': return 5;
        case 'j': return 6; case 'k': return 6; case 'l': return 6;
        case 'm': return 7; case 'n': return 7; case 'o': return 5;
        case 'p': return 5; case 'q': return 2; case 'r': return 2;
        case 's': return 1; case 't': return 2; case 'u': return 5;
        case 'v': return 0; case 'w': return 2; case 'x': return 0;
        case 'y': return 5; case 'z': return 0;
        default: return -1;
    }
}

static int key_bit(char c)
{
    switch (c) {
        case ' ': return 0; case '0': return 0; case '1': return 0;
        case '2': return 1; case '3': return 2; case '4': return 3;
        case '5': return 4; case '6': return 4; case '7': return 3;
        case '8': return 2; case '9': return 1;
        case 'a': return 0; case 'b': return 4; case 'c': return 3;
        case 'd': return 2; case 'e': return 2; case 'f': return 3;
        case 'g': return 4; case 'h': return 4; case 'i': return 2;
        case 'j': return 3; case 'k': return 2; case 'l': return 1;
        case 'm': return 2; case 'n': return 3; case 'o': return 1;
        case 'p': return 0; case 'q': return 0; case 'r': return 3;
        case 's': return 1; case 't': return 4; case 'u': return 3;
        case 'v': return 4; case 'w': return 1; case 'x': return 2;
        case 'y': return 4; case 'z': return 1;
        default: return -1;
    }
}

#define CAPS_BYTE 0
#define CAPS_BIT  0
#define SYM_BYTE  7
#define SYM_BIT   1

static void update_kb_state(void) { spkb_refresh(); }

static void set_spec_key(int byte_idx, int bit, bool pressed)
{
    byte mask = (byte)(1u << bit);
    if (pressed)
        spkey_state[byte_idx] |= mask;
    else
        spkey_state[byte_idx] &= ~mask;
}

static const struct { char sym; int row; int bit; } sym_map[] = {
    {'!', 3, 0}, {'"', 5, 0}, {'#', 3, 2}, {'$', 3, 3}, {'%', 3, 4},
    {'&', 4, 4}, {'\'', 4, 3}, {'(', 4, 2}, {')', 4, 1},
    {'*', 7, 4}, {'+', 6, 2}, {',', 7, 3}, {'-', 6, 3},
    {'.', 7, 2}, {'/', 0, 4},
    {':', 0, 1}, {';', 5, 1}, {'<', 2, 3}, {'=', 6, 1},
    {'>', 2, 4}, {'?', 0, 3}, {'@', 3, 1},
    {'[', 5, 4}, {'\\', 1, 2}, {']', 5, 3}, {'^', 6, 4},
    {'_', 4, 0}, {'`', 0, 2},
    {'{', 1, 3}, {'|', 1, 1}, {'}', 1, 4}, {'~', 1, 0},
};

static void handle_key_press(char key, bool pressed, uint8_t modifiers, uint8_t raw)
{
    (void)raw;
    if (modifiers & MODIFIER_CTRL) return;

    if (key == '\r' || key == '\n') {
        set_spec_key(6, 0, pressed);
        update_kb_state();
        return;
    }

    if (key == 27) return;

    if (key == 8 || key == 127) {
        set_spec_key(CAPS_BYTE, CAPS_BIT, pressed);
        set_spec_key(4, 0, pressed);
        update_kb_state();
        return;
    }

    if (key >= 'A' && key <= 'Z') {
        key += 32;
        set_spec_key(CAPS_BYTE, CAPS_BIT, pressed);
    } else if (key < 0 || key > 127) {
        return;
    }

    if (key >= 32 && key <= 127) {
        for (size_t i = 0; i < sizeof(sym_map) / sizeof(sym_map[0]); i++) {
            if (sym_map[i].sym == key) {
                set_spec_key(SYM_BYTE, SYM_BIT, pressed);
                set_spec_key(sym_map[i].row, sym_map[i].bit, pressed);
                update_kb_state();
                return;
            }
        }
    }

    if (key >= 0 && key < 128) {
        int byte_idx = key_row(key);
        int bit = key_bit(key);
        if (byte_idx >= 0) {
            set_spec_key(byte_idx, bit, pressed);
            update_kb_state();
        }
    }
}

static void clear_all_keys(void)
{
    memset(spkey_state, 0, sizeof(spkey_state));
    spkb_refresh();
}

static void run_emulation_frame(void)
{
    static int tc = 0;

    tc = sp_halfframe(tc, EVENHF);
    tc = sp_halfframe(tc, ODDHF);
    play_sound(0);
    translate_screen();
    graphics_flush();
    z80_interrupt(0);
}

// File list helpers

static bool has_spectrum_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 4) return false;
    if (mis_strcasecmp(name + len - 3, "z80") == 0) return true;
    if (mis_strcasecmp(name + len - 3, "sna") == 0) return true;
    if (mis_strcasecmp(name + len - 3, "tap") == 0) return true;
    return false;
}

static file_list_t *scan_files(void)
{
    file_list_t *list = calloc(1, sizeof(file_list_t));
    if (!list) return NULL;

    DIR *dir = opendir(ROMS_DIR);
    if (!dir) {
        mkdir(ROMS_DIR, 0777);
        return list;
    }

    char *tmp_names[MAX_FILES];
    char *tmp_paths[MAX_FILES];
    size_t count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        if (entry->d_name[0] == '.') continue;
        if (!has_spectrum_extension(entry->d_name)) continue;

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
            free(tmp_names[count]);
            free(tmp_paths[count]);
        }
    }
    closedir(dir);

    // Add built-in entries
    if (count < MAX_FILES) {
        tmp_names[count] = malloc(6);
        tmp_paths[count] = malloc(sizeof(BUILTIN_BASIC_PATH));
        if (tmp_names[count] && tmp_paths[count]) {
            strcpy(tmp_names[count], "BASIC");
            strcpy(tmp_paths[count], BUILTIN_BASIC_PATH);
            count++;
        } else { free(tmp_names[count]); free(tmp_paths[count]); }
    }
    if (count < MAX_FILES) {
        tmp_names[count] = malloc(12);
        tmp_paths[count] = malloc(sizeof(BUILTIN_MM_PATH));
        if (tmp_names[count] && tmp_paths[count]) {
            strcpy(tmp_names[count], "Manic Miner");
            strcpy(tmp_paths[count], BUILTIN_MM_PATH);
            count++;
        } else { free(tmp_names[count]); free(tmp_paths[count]); }
    }

    // Bubble sort
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (strcmp(tmp_names[i], tmp_names[j]) > 0) {
                char *tn = tmp_names[i]; tmp_names[i] = tmp_names[j]; tmp_names[j] = tn;
                char *tp = tmp_paths[i]; tmp_paths[i] = tmp_paths[j]; tmp_paths[j] = tp;
            }
        }
    }

    list->names = calloc(count, sizeof(char *));
    list->paths = calloc(count, sizeof(char *));
    if (count > 0 && (!list->names || !list->paths)) {
        for (size_t i = 0; i < count; i++) { free(tmp_names[i]); free(tmp_paths[i]); }
        free(list->names);
        free(list->paths);
        list->names = NULL;
        list->paths = NULL;
        return list;
    }

    list->count = count;
    list->selected = 0;
    if (count > 0) {
        memcpy(list->names, tmp_names, count * sizeof(char *));
        memcpy(list->paths, tmp_paths, count * sizeof(char *));
    }

    return list;
}

static void free_file_list_data(void)
{
    if (!file_list_data) return;
    if (file_list_data->names) {
        for (int i = 0; i < file_list_data->count; i++)
            free(file_list_data->names[i]);
        free(file_list_data->names);
    }
    if (file_list_data->paths) {
        for (int i = 0; i < file_list_data->count; i++)
            free(file_list_data->paths[i]);
        free(file_list_data->paths);
    }
    free(file_list_data);
    file_list_data = NULL;
}

static void cleanup_emulator(void)
{
    clear_all_keys();
    audio_stop();
}

static void open_selected_file(void);
static void show_file_list(void);

static void on_file_activated(int item_index, void *user_data)
{
    (void)user_data;
    os_log("SPECTRUM", "on_file_activated(%d)", item_index);
    if (file_list_data) file_list_data->selected = item_index;
    open_selected_file();
}

static void on_selection_changed(int new_selection, void *user_data)
{
    (void)user_data;
    if (file_list_data) file_list_data->selected = new_selection;
}

static void on_up_click(ui2_button_t *button, void *user_data)
{
    (void)button; (void)user_data;
    if (file_list) {
        int sel = ui2_list_get_selection(file_list);
        if (sel > 0) ui2_list_set_selection(file_list, sel - 1);
    }
}

static void on_down_click(ui2_button_t *button, void *user_data)
{
    (void)button; (void)user_data;
    if (file_list) {
        int sel = ui2_list_get_selection(file_list);
        if (sel < file_list->count - 1) ui2_list_set_selection(file_list, sel + 1);
    }
}

static void on_open_click(ui2_button_t *button, void *user_data)
{
    (void)button; (void)user_data;
    open_selected_file();
}

static void on_exit_click(ui2_button_t *button, void *user_data)
{
    (void)button; (void)user_data;
    os_exit();
}

static void destroy_screen(void)
{
    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
        file_list = NULL;
    }
}

static void build_file_list_screen(void)
{
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_height = rows - 5;

    screen = ui2_screen_create();
    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_screen_set_root(screen, root);

    if (file_list_data && file_list_data->count > 0) {
        file_list = ui2_list_create(1, 1, cols - 2, list_height);
        if (!file_list) return;
        ui2_list_set_title(file_list, "Select a Spectrum file");
        ui2_list_set_colors(file_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                            TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_CYAN);
        ui2_list_set_items(file_list, (const char **)file_list_data->names,
                           file_list_data->count);
        ui2_list_set_selection(file_list, file_list_data->selected);
        ui2_list_set_callbacks(file_list, on_selection_changed, on_file_activated, NULL);
        ui2_layout_add(root, UI2_WIDGET(file_list));
        ui2_screen_focus_set(screen, UI2_WIDGET(file_list));
    } else {
        ui2_label_t *msg = ui2_label_create(2, 2, "No Spectrum files found",
                                            TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
        ui2_label_t *dir_msg = ui2_label_create(2, 4, "Place .z80/.sna/.tap files in",
                                                TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
        ui2_label_t *dir_path = ui2_label_create(2, 5, ROMS_DIR,
                                                 TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
        ui2_layout_add(root, UI2_WIDGET(msg));
        ui2_layout_add(root, UI2_WIDGET(dir_msg));
        ui2_layout_add(root, UI2_WIDGET(dir_path));
    }

    ui2_toolbar_item_t items[] = {
        {"Up",   on_up_click,   NULL},
        {"Dn",   on_down_click, NULL},
        {"Open", on_open_click, NULL},
        {ICON_X, on_exit_click, NULL},
    };
    ui2_layout_t *bar = ui2_toolbar_create(1, rows - 3, cols - 2, 3, items, 4);
    ui2_layout_add(root, UI2_WIDGET(bar));

    ui2_screen_render(screen);
}

static bool start_emulator(const char *path)
{
    char *path_copy = strdup(path);
    if (!path_copy) return false;

    destroy_screen();
    free_file_list_data();

    bool ok = false;

    if (strcmp(path_copy, BUILTIN_BASIC_PATH) == 0) {
        // BASIC mode: emulator is already initialized from app_init, just run it
        ok = true;
    } else if (strcmp(path_copy, BUILTIN_MM_PATH) == 0) {
        ok = loader_load_z80_from_buffer(game, sizeof(game)) == 0;
    } else {
        int type = loader_get_type(path_copy);
        if (type == LOADER_Z80 || type == LOADER_SNA) {
            ok = loader_load_z80(path_copy) == 0;
        }
    }

    free(path_copy);

    if (!ok) return false;

    int scr_w = display_get_width();
    int scr_h = display_get_height();
    size_t fb_size = (size_t)scr_w * scr_h / 2;
    graphics_mode_deinit();
    graphics_mode_init(fb_buffer, fb_size);
    spscr_init_colors();

    app_mode = MODE_PLAYING;
    return true;
}

static void open_selected_file(void)
{
    if (!file_list_data || file_list_data->count <= 0 ||
        file_list_data->selected < 0 || file_list_data->selected >= file_list_data->count)
        return;

    if (!start_emulator(file_list_data->paths[file_list_data->selected]))
        show_file_list();
}

static void show_file_list(void)
{
    app_mode = MODE_FILE_LIST;
    cleanup_emulator();
    free_file_list_data();
    file_list_data = scan_files();
    text_mode_init();
    destroy_screen();
    build_file_list_screen();
}

void app_init(app_context_t *ctx)
{
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER | EVENT_TOUCH;
    ctx->timer_interval_ms = 20;
    ctx->requested_cpu_freq_mhz = 240;

    int scr_w = display_get_width();
    int scr_h = display_get_height();
    size_t fb_size = (size_t)scr_w * scr_h / 2;
    fb_buffer = malloc(fb_size);
    if (!fb_buffer) return;

    graphics_mode_init(fb_buffer, fb_size);
    spscr_init_colors();

    memset(spkey_state, 0, sizeof(spkey_state));
    spscr_init_line_pointers(scr_h);
    sp_init();
    sp_image = (char *)z80_proc.mem + 0x4000;
    init_basekeys();

    graphics_clear(7);
    graphics_flush();

    char startup_file[256];
    size_t file_len = os_consume_startup_file(startup_file, sizeof(startup_file) - 1);
    if (file_len > 0) {
        startup_file[file_len] = '\0';
        if (start_emulator(startup_file)) {
            initialized = true;
            return;
        }
    }

    show_file_list();
    initialized = true;
}

void app_event(app_context_t *ctx, event_t *event)
{
    (void)ctx;
    if (!initialized) return;

    if (app_mode == MODE_PLAYING) {
        if (event->type == EVENT_KEYBOARD) {
            if (event->keyboard.pressed && event->keyboard.key == 27) {
                show_file_list();
                return;
            }
            handle_key_press(event->keyboard.key,
                             event->keyboard.pressed,
                             event->keyboard.modifiers,
                             event->keyboard.raw_key_code);
        } else if (event->type == EVENT_TIMER) {
            run_emulation_frame();
        }
        return;
    }

    if (app_mode == MODE_FILE_LIST) {
        if (event->type == EVENT_KEYBOARD) {
            if (event->keyboard.pressed) {
                os_log("SPECTRUM", "kbd key=%d mods=%d", event->keyboard.key, event->keyboard.modifiers);
                if (ui2_screen_handle_event(screen, event)) {
                    ui2_screen_render(screen);
                    return;
                }
                if (event->keyboard.key == 27) {
                    os_exit();
                }
            }
            return;
        }
        if (event->type == EVENT_TOUCH) {
            os_log("SPECTRUM", "touch at (%d,%d) pressed=%d",
                   event->touch.x, event->touch.y, event->touch.pressed);
            if (ui2_screen_handle_event(screen, event)) {
                ui2_screen_render(screen);
            }
            return;
        }
    }
}

void app_checkpoint(app_context_t *ctx)
{
    (void)ctx;
}

void app_close(app_context_t *ctx)
{
    (void)ctx;
    cleanup_emulator();
    graphics_mode_deinit();
    if (fb_buffer) { free(fb_buffer); fb_buffer = NULL; }
    initialized = false;
}
