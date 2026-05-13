#include "os_core.h"
#include "text_mode.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "sd_test";

#define MAX_ENTRIES 256
#define NAME_LEN 128

static int entry_count = 0;
static char entries[MAX_ENTRIES][NAME_LEN];
static int entry_is_dir[MAX_ENTRIES];
static int scroll_offset = 0;

static void scan_dir(const char *full_base, const char *rel_prefix) {
    DIR *dir = opendir(full_base);
    if (!dir) return;

    struct dirent *dent;
    while ((dent = readdir(dir)) != NULL && entry_count < MAX_ENTRIES) {
        if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0) continue;

        char child_full[256];
        snprintf(child_full, sizeof(child_full), "%s/%s", full_base, dent->d_name);

        struct stat st;
        int is_dir = (stat(child_full, &st) == 0 && S_ISDIR(st.st_mode));

        if (rel_prefix[0]) {
            snprintf(entries[entry_count], NAME_LEN, "%s/%s", rel_prefix, dent->d_name);
        } else {
            strncpy(entries[entry_count], dent->d_name, NAME_LEN - 1);
            entries[entry_count][NAME_LEN - 1] = '\0';
        }
        entry_is_dir[entry_count] = is_dir;
        entry_count++;

        if (is_dir) {
            scan_dir(child_full, entries[entry_count - 1]);
        }
    }
    closedir(dir);
}

static void load_directory(void) {
    entry_count = 0;
    scan_dir("/sdcard", "");
    os_log(TAG, "Found %d entries on SD card", entry_count);
}

static void draw_screen(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_rows = rows - 3;

    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(0, 0, "SD Card Contents", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    if (entry_count == 0) {
        text_mode_print_at_attr(0, 2, "(empty or no SD card)", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    }

    for (int i = 0; i < list_rows && (scroll_offset + i) < entry_count; i++) {
        int idx = scroll_offset + i;
        uint8_t color = entry_is_dir[idx] ? TEXT_COLOR_YELLOW : TEXT_COLOR_WHITE;
        int max_name = cols - 1;
        char line[256];
        snprintf(line, sizeof(line), "%.*s", max_name, entries[idx]);
        text_mode_print_at_attr(0, i + 1, line, color, TEXT_ATTR_NORMAL);
    }

    text_mode_printf_at_attr(0, rows - 1, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL,
                             "W/S: scroll  (%d items)", entry_count);
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    text_mode_init();
    load_directory();
    scroll_offset = 0;
    draw_screen();

    os_log(TAG, "SD test started");
}

void app_checkpoint(app_context_t *ctx) {
}

void app_close(app_context_t *ctx) {
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "SD test closing");
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        int rows = text_mode_get_rows();
        int list_rows = rows - 3;

        if (event->keyboard.key == 'w' || event->keyboard.key == 'W') {
            if (scroll_offset > 0) {
                scroll_offset--;
                draw_screen();
            }
        } else if (event->keyboard.key == 's' || event->keyboard.key == 'S') {
            if (scroll_offset + list_rows < entry_count) {
                scroll_offset++;
                draw_screen();
            }
        }
    }
}
