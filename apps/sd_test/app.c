#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
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
static struct stat entry_stat[MAX_ENTRIES];
static const char *item_ptrs[MAX_ENTRIES];
static int selected = 0;
static int scroll_offset = 0;
static int detail_mode = 0;

static void scan_dir(const char *full_base, const char *rel_prefix) {
    DIR *dir = opendir(full_base);
    if (!dir) return;

    struct dirent *dent;
    while ((dent = readdir(dir)) != NULL && entry_count < MAX_ENTRIES) {
        if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0) continue;

        char child_full[256];
        snprintf(child_full, sizeof(child_full), "%s/%s", full_base, dent->d_name);

        if (rel_prefix[0]) {
            snprintf(entries[entry_count], NAME_LEN, "%s/%s", rel_prefix, dent->d_name);
        } else {
            strncpy(entries[entry_count], dent->d_name, NAME_LEN - 1);
            entries[entry_count][NAME_LEN - 1] = '\0';
        }
        item_ptrs[entry_count] = entries[entry_count];

        int is_dir = (stat(child_full, &entry_stat[entry_count]) == 0 && S_ISDIR(entry_stat[entry_count].st_mode));
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

static void draw_list(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();
    int list_rows = rows - 2;

    ui_clear();
    ui_window(0, 0, cols, rows, "SD Card Browser");
    ui_menu_draw(1, 1, list_rows - 1, item_ptrs, entry_count, selected);
    ui_status_bar(rows - 1, "W/S: nav  ENTER: info", "ESC: exit");
}

static void draw_detail(void) {
    int rows = text_mode_get_rows();
    int cols = text_mode_get_cols();

    ui_clear();
    ui_window(0, 0, cols, rows, "File Info");

    struct stat *st = &entry_stat[selected];
    char buf[128];

    text_mode_print_at_attr(1, 1, entries[selected], TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    snprintf(buf, sizeof(buf), "Size: %d bytes", (int)st->st_size);
    text_mode_print_at_attr(1, 3, buf, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    snprintf(buf, sizeof(buf), "Mode: 0%o", (unsigned int)st->st_mode);
    text_mode_print_at_attr(1, 4, buf, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr(1, 5, S_ISDIR(st->st_mode) ? "Type: directory" : "Type: file", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    snprintf(buf, sizeof(buf), "Inode: %d", (int)st->st_ino);
    text_mode_print_at_attr(1, 6, buf, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    snprintf(buf, sizeof(buf), "Links: %d", (int)st->st_nlink);
    text_mode_print_at_attr(1, 7, buf, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    ui_status_bar(rows - 1, "ESC: back", "");
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    text_mode_init();
    load_directory();
    selected = 0;
    scroll_offset = 0;
    detail_mode = 0;
    draw_list();
}

void app_checkpoint(app_context_t *ctx) {
}

void app_close(app_context_t *ctx) {
    text_mode_clear(TEXT_COLOR_BLACK);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (!(event->type == EVENT_KEYBOARD && event->keyboard.pressed)) return;

    int rows = text_mode_get_rows();
    int list_rows = rows - 3;
    char key = event->keyboard.key;

    if (detail_mode) {
        if (key == 27) {
            detail_mode = 0;
            draw_list();
        }
        return;
    }

    if (key == 'w' || key == 'W') {
        if (selected > 0) {
            selected--;
            if (selected < scroll_offset) scroll_offset = selected;
            draw_list();
        }
    } else if (key == 's' || key == 'S') {
        if (selected < entry_count - 1) {
            selected++;
            if (selected >= scroll_offset + list_rows) scroll_offset = selected - list_rows + 1;
            draw_list();
        }
    } else if (key == '\n' || key == '\r') {
        detail_mode = 1;
        draw_detail();
    } else if (key == 27) {
        text_mode_clear(TEXT_COLOR_BLACK);
    }
}
