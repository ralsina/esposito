#include "reader_toc.h"

#include "checkpoint.h"
#include "reader_md.h"
#include "reader_page.h"
#include "text_mode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define KEY_TOC_PREFIX       "rtoc"
#define KEY_TOC_MTIME_PREFIX "rtoc_mt"
#define KEY_TOC_VER_PREFIX   "rtoc_v"
#define KEY_TOC_TOTAL_PREFIX "rtoc_tp"
#define TOC_CACHE_VERSION    5

// Field and record separators for serialized TOC
#define TOC_FIELD_SEP '\x01'
#define TOC_REC_SEP   '\x02'

static void draw_scan_progress(const char *path, long scanned, long total, int page, int toc_count, int force) {
    static int last_percent = -1;
    static int last_page = 0;
    static int last_fill_w = -1;
    static int screen_initialized = 0;
    static char last_path[320];

    if (total <= 0) {
        total = 1;
    }

    int percent = (int)((scanned * 100L) / total);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Throttle updates to avoid excessive redraw/flicker.
    if (!force && percent == last_percent && (page - last_page) < 4) {
        return;
    }

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    if (!screen_initialized || force || strcmp(last_path, path) != 0) {
        ui_clear();

        const char *name = path;
        const char *slash = strrchr(path, '/');
        if (slash && slash[1]) {
            name = slash + 1;
        }

        text_mode_print_at_attr(2, 2, "Building TOC...", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
        text_mode_print_at_color(2, 4, name, TEXT_COLOR_WHITE);
        text_mode_print_at_color(2, rows - 2, "Please wait...", TEXT_COLOR_BRIGHT_BLACK);

        strncpy(last_path, path, sizeof(last_path) - 1);
        last_path[sizeof(last_path) - 1] = '\0';
        screen_initialized = 1;
        last_fill_w = -1;
    }

    int bar_x = 2;
    int bar_y = 6;
    int bar_w = cols - 4;
    if (bar_w < 10) {
        bar_w = 10;
    }
    int fill_w = (bar_w * percent) / 100;

    if (last_fill_w < 0) {
        // First draw: initialize entire bar area once.
        for (int i = 0; i < bar_w; i++) {
            text_mode_print_at_color(bar_x + i, bar_y, "#", TEXT_COLOR_BRIGHT_BLACK);
        }
    }

    if (fill_w > last_fill_w) {
        for (int i = last_fill_w < 0 ? 0 : last_fill_w; i < fill_w; i++) {
            text_mode_print_at_color(bar_x + i, bar_y, "#", TEXT_COLOR_GREEN);
        }
    } else if (fill_w < last_fill_w) {
        for (int i = fill_w; i < last_fill_w; i++) {
            text_mode_print_at_color(bar_x + i, bar_y, "#", TEXT_COLOR_BRIGHT_BLACK);
        }
    }
    last_fill_w = fill_w;

    char stats[64];
    snprintf(stats, sizeof(stats), "%d%%  page %d  headings %d", percent, page, toc_count);
    for (int i = 2; i < cols - 2; i++) {
        text_mode_print_at_color(i, 8, " ", TEXT_COLOR_BLACK);
    }
    text_mode_print_at_color(2, 8, stats, TEXT_COLOR_CYAN);

    last_percent = percent;
    last_page = page;
    text_mode_flush();
}

static void build_toc_key(char *out, size_t out_size, const char *prefix, const char *path) {
    snprintf(out, out_size, "%s:%s", prefix, path);
}

// Strip markdown formatting markers from rendered text to get a clean title
static void strip_markers(const char *src, char *dst, size_t dst_size) {
    size_t out = 0;
    while (*src && out < dst_size - 1) {
        if (src[0] == '*' && src[1] == '*') {
            src += 2;
            continue;
        }
        if ((unsigned char)*src == (unsigned char)MD_FORMAT_TOGGLE) {
            src++;
            continue;
        }
        dst[out++] = *src++;
    }
    dst[out] = '\0';
}

static void scan_and_build(reader_state_t *state) {
    state->toc_count = 0;
    state->total_pages = 0;

    if (!state->file) return;

    // Save position and md state, then rewind
    long saved_pos = ftell(state->file);
    struct stat st;
    long total_size = 0;
    if (stat(state->current_file, &st) == 0) {
        total_size = (long)st.st_size;
    }

    draw_scan_progress(state->current_file, 0, total_size, 1, 0, 1);

    md_clear_remainder();
    fseek(state->file, 0, SEEK_SET);

    int screen_width = state->screen_width > 0 ? state->screen_width : 36;
    int content_rows = state->content_rows > 0 ? state->content_rows : 26;

    rendered_line_t *scan_lines = malloc(sizeof(rendered_line_t) * MAX_RENDERED_LINES);
    uint8_t *scan_levels = malloc(sizeof(uint8_t) * MAX_RENDERED_LINES);
    if (!scan_lines || !scan_levels) {
        // Keep reader usable even if TOC scan cannot allocate scratch memory.
        if (scan_lines) free(scan_lines);
        if (scan_levels) free(scan_levels);
        md_clear_remainder();
        fseek(state->file, saved_pos, SEEK_SET);
        return;
    }

    int page = 1;

    while (state->toc_count < MAX_TOC_ENTRIES) {
        uint32_t page_start = (uint32_t)ftell(state->file);
        int count = md_scan_page_with_levels(state->file, scan_lines, scan_levels, content_rows, screen_width);
        if (count == 0) break;

        // Detect headings across the full page, not only at top.
        int in_heading_block = 0;
        for (int i = 0; i < count && state->toc_count < MAX_TOC_ENTRIES; i++) {
            if (scan_lines[i].text[0] == '\0') {
                in_heading_block = 0;
                continue;
            }

            int heading_level = (int)scan_levels[i];
            int is_heading = heading_level > 0;

            // For wrapped headings, keep only the first rendered line.
            if (is_heading && !in_heading_block) {
                toc_entry_t *entry = &state->toc[state->toc_count++];
                strip_markers(scan_lines[i].text, entry->title, sizeof(entry->title));
                entry->level = (uint8_t)heading_level;
                if (entry->level < 1) {
                    entry->level = 1;
                }
                if (entry->level > 6) {
                    entry->level = 6;
                }
                entry->page_number = page;
                entry->file_offset = page_start;
                in_heading_block = 1;
            } else if (!is_heading) {
                in_heading_block = 0;
            }
        }

        long scanned = ftell(state->file);
        if (scanned < 0) {
            scanned = 0;
        }
        draw_scan_progress(state->current_file, scanned, total_size, page, state->toc_count, 0);

        page++;
    }

    state->total_pages = page - 1;
    draw_scan_progress(state->current_file, total_size, total_size, state->total_pages, state->toc_count, 1);

    // Restore file position and md state
    free(scan_levels);
    free(scan_lines);
    md_clear_remainder();
    fseek(state->file, saved_pos, SEEK_SET);
}

static void save_toc_to_checkpoint(const reader_state_t *state, long mtime) {
    char key[320];
    char mtime_key[320];
    char ver_key[320];
    char total_key[320];
    build_toc_key(key, sizeof(key), KEY_TOC_PREFIX, state->current_file);
    build_toc_key(mtime_key, sizeof(mtime_key), KEY_TOC_MTIME_PREFIX, state->current_file);
    build_toc_key(ver_key, sizeof(ver_key), KEY_TOC_VER_PREFIX, state->current_file);
    build_toc_key(total_key, sizeof(total_key), KEY_TOC_TOTAL_PREFIX, state->current_file);

    checkpoint_save_int(mtime_key, (int)mtime);
    checkpoint_save_int(ver_key, TOC_CACHE_VERSION);
    checkpoint_save_int(total_key, state->total_pages);

    if (state->toc_count == 0) {
        checkpoint_save_string(key, "");
        return;
    }

    // Serialize: title\x01level\x01page\x01offset\x02...
    char *serialize_buf = malloc(2048);
    if (!serialize_buf) {
        checkpoint_save_string(key, "");
        return;
    }

    int pos = 0;

    for (int i = 0; i < state->toc_count && pos < 2048 - 64; i++) {
        if (i > 0) serialize_buf[pos++] = TOC_REC_SEP;
        int n = snprintf(serialize_buf + pos, 2048 - pos, "%s%c%d%c%d%c%lu",
                         state->toc[i].title,
                         TOC_FIELD_SEP,
                         state->toc[i].level,
                         TOC_FIELD_SEP,
                         state->toc[i].page_number,
                         TOC_FIELD_SEP,
                         (unsigned long)state->toc[i].file_offset);
        if (n > 0) pos += n;
    }
    serialize_buf[pos] = '\0';

    checkpoint_save_string(key, serialize_buf);
    free(serialize_buf);
}

static int load_toc_from_checkpoint(reader_state_t *state, long file_mtime) {
    char key[320];
    char mtime_key[320];
    char ver_key[320];
    char total_key[320];
    build_toc_key(key, sizeof(key), KEY_TOC_PREFIX, state->current_file);
    build_toc_key(mtime_key, sizeof(mtime_key), KEY_TOC_MTIME_PREFIX, state->current_file);
    build_toc_key(ver_key, sizeof(ver_key), KEY_TOC_VER_PREFIX, state->current_file);
    build_toc_key(total_key, sizeof(total_key), KEY_TOC_TOTAL_PREFIX, state->current_file);

    int saved_ver = checkpoint_load_int(ver_key);
    if (saved_ver != TOC_CACHE_VERSION) {
        return 0;
    }

    int saved_mtime = checkpoint_load_int(mtime_key);
    if (saved_mtime != (int)file_mtime) {
        return 0;  // Stale or missing
    }

    state->total_pages = checkpoint_load_int(total_key);
    if (state->total_pages < 0) {
        state->total_pages = 0;
    }

    const char *buf = checkpoint_load_string(key);
    if (!buf || !buf[0]) {
        return saved_mtime == (int)file_mtime;  // Empty TOC is valid if mtime matches
    }

    state->toc_count = 0;
    size_t parse_len = strlen(buf);
    char *parse_buf = malloc(parse_len + 1);
    if (!parse_buf) {
        return 0;
    }
    memcpy(parse_buf, buf, parse_len + 1);

    char *rec = parse_buf;
    while (*rec && state->toc_count < MAX_TOC_ENTRIES) {
        char *rec_end = strchr(rec, TOC_REC_SEP);
        if (rec_end) *rec_end = '\0';

        char *f1 = strchr(rec, TOC_FIELD_SEP);
        if (!f1) break;
        *f1 = '\0';
        char *f2 = strchr(f1 + 1, TOC_FIELD_SEP);
        if (!f2) break;
        *f2 = '\0';
        char *f3 = strchr(f2 + 1, TOC_FIELD_SEP);
        if (!f3) break;
        *f3 = '\0';

        toc_entry_t *entry = &state->toc[state->toc_count++];
        strncpy(entry->title, rec, sizeof(entry->title) - 1);
        entry->title[sizeof(entry->title) - 1] = '\0';
        entry->level = (uint8_t)atoi(f1 + 1);
        if (entry->level < 1) {
            entry->level = 1;
        }
        entry->page_number = atoi(f2 + 1);
        entry->file_offset = (uint32_t)atol(f3 + 1);

        if (rec_end) rec = rec_end + 1;
        else break;
    }

    free(parse_buf);

    return 1;
}

void reader_toc_load_or_build(reader_state_t *state) {
    state->toc_count = 0;
    state->toc_selected = 0;
    state->total_pages = 0;

    if (!state->file || !state->current_file[0]) return;

    struct stat st;
    long mtime = 0;
    if (stat(state->current_file, &st) == 0) {
        mtime = (long)st.st_mtime;
    }

    if (load_toc_from_checkpoint(state, mtime)) {
        return;  // Cache hit
    }

    scan_and_build(state);
    save_toc_to_checkpoint(state, mtime);
}

void reader_toc_clear(reader_state_t *state) {
    state->toc_count = 0;
    state->toc_selected = 0;
    state->total_pages = 0;
}
