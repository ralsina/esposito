#include "reader_core.h"

#include "app_config.h"
#include "reader_toc.h"
#include "reader_render_pipeline.h"
#include <text_mode.h>
#include <os_core.h>
#include <esp_timer.h>

#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int is_safe_path_char(char ch) {
    if ((ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '_' || ch == '-' || ch == '.') {
        return 1;
    }
    return 0;
}

void reader_build_book_key(char *out, size_t out_size, const char *prefix, const char *path) {
    char safe_path[240];
    size_t out_index = 0;

    for (size_t index = 0; path[index] && out_index < sizeof(safe_path) - 1; index++) {
        char ch = path[index];
        if (is_safe_path_char(ch)) {
            safe_path[out_index++] = (char)ch;
        } else {
            safe_path[out_index++] = '_';
        }
    }
    safe_path[out_index] = '\0';

    snprintf(out, out_size, "%s/%s", prefix, safe_path);
}

void reader_save_current_book_progress(reader_state_t *state, bool force) {
    if (!state->file || !state->current_file[0] || state->page_cache.current < 0) {
        return;
    }

    if (!force) {
        int64_t now = esp_timer_get_time();
        if (now - state->last_save_us < 5000000) {
            return;
        }
        state->last_save_us = now;
    }

    config_bind_app("reader");

    char offset_key[320];
    reader_build_book_key(offset_key, sizeof(offset_key), KEY_BOOK_OFFSET_PREFIX, state->current_file);

    config_set_int(offset_key, (int)state->page_cache.entries[state->page_cache.current].file_pos);
    config_set_string(KEY_LAST_FILE, state->current_file);
}

void reader_free_file_list(reader_state_t *state) {
    if (state->file_names) {
        free(state->file_names);
        state->file_names = NULL;
    }
    if (state->file_paths) {
        free(state->file_paths);
        state->file_paths = NULL;
    }
    if (state->file_ptrs) {
        free(state->file_ptrs);
        state->file_ptrs = NULL;
    }
    state->file_count = 0;
    state->file_selected = 0;
}

void reader_free_toc_titles(reader_state_t *state) {
    if (state->toc_titles) {
        free(state->toc_titles);
        state->toc_titles = NULL;
    }
}

void reader_scan_md_files(reader_state_t *state) {
    reader_free_file_list(state);

    state->file_names = malloc(sizeof(*state->file_names) * MAX_FILES);
    state->file_paths = malloc(sizeof(*state->file_paths) * MAX_FILES);
    state->file_ptrs = malloc(sizeof(*state->file_ptrs) * MAX_FILES);
    if (!state->file_names || !state->file_paths || !state->file_ptrs) {
        reader_free_file_list(state);
        return;
    }

    state->file_count = 0;
    DIR *dir = opendir("/sdcard/books");
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && state->file_count < MAX_FILES) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t len = strlen(entry->d_name);
        if (len < 3 || strcmp(entry->d_name + len - 3, ".md") != 0) {
            continue;
        }

        // Store full path
        snprintf(state->file_paths[state->file_count], MAX_PATH, "/sdcard/books/%s", entry->d_name);

        // Store display name without .md extension
        size_t name_len = len - 3;
        if (name_len > 63) name_len = 63;
        memcpy(state->file_names[state->file_count], entry->d_name, name_len);
        state->file_names[state->file_count][name_len] = '\0';

        state->file_ptrs[state->file_count] = state->file_names[state->file_count];
        state->file_count++;
    }

    closedir(dir);

    // Sort alphabetically (case-insensitive)
    if (state->file_count > 1) {
        // Bubble sort file_names, file_paths, and file_ptrs together
        for (int i = 0; i < state->file_count - 1; i++) {
            for (int j = 0; j < state->file_count - 1 - i; j++) {
                // Compare lowercase versions for case-insensitive sort
                const char *a = state->file_names[j];
                const char *b = state->file_names[j + 1];
                char ca, cb;
                int cmp = 0;
                while (1) {
                    ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
                    cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
                    if (ca != cb || ca == '\0') {
                        cmp = ca - cb;
                        break;
                    }
                    a++; b++;
                }
                if (cmp > 0) {
                    // Swap file_names
                    char temp[64];
                    memcpy(temp, state->file_names[j], 64);
                    memcpy(state->file_names[j], state->file_names[j + 1], 64);
                    memcpy(state->file_names[j + 1], temp, 64);
                    // Swap file_paths
                    char temp_path[MAX_PATH];
                    memcpy(temp_path, state->file_paths[j], MAX_PATH);
                    memcpy(state->file_paths[j], state->file_paths[j + 1], MAX_PATH);
                    memcpy(state->file_paths[j + 1], temp_path, MAX_PATH);
                    // Swap file_ptrs
                    state->file_ptrs[j] = state->file_names[j];
                    state->file_ptrs[j + 1] = state->file_names[j + 1];
                }
            }
        }
    }
}

int reader_find_file_index_by_path(const reader_state_t *state, const char *path) {
    if (!path || !path[0]) {
        return -1;
    }

    for (int index = 0; index < state->file_count; index++) {
        if (strcmp(state->file_paths[index], path) == 0) {
            return index;
        }
    }

    return -1;
}

void reader_close_current_file(reader_state_t *state) {
    if (state->file) {
        reader_save_current_book_progress(state, true);
        fclose(state->file);
        state->file = NULL;
    }

    reader_toc_clear(state);
    state->current_file[0] = '\0';
}

int reader_open_file(reader_state_t *state, const char *path) {
    reader_close_current_file(state);

    state->file = fopen(path, "r");
    if (!state->file) {
        return 0;
    }

    strncpy(state->current_file, path, MAX_PATH - 1);
    state->current_file[MAX_PATH - 1] = '\0';
    page_cache_init(&state->page_cache);
    state->page_cache.entries[0].file_pos = 0;
    state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
    state->page_cache.entries[0].screen_width = 0;  // Will be set when entering reading mode
    state->page_cache.entries[0].content_rows = 0;
    state->page_cache.count = 1;
    state->page_cache.current = 0;
    state->page_number = 1;

    config_bind_app("reader");

    char offset_key[320];
    reader_build_book_key(offset_key, sizeof(offset_key), KEY_BOOK_OFFSET_PREFIX, path);

    int saved_offset = config_get_int(offset_key, 0);

    if (saved_offset > 0) {
        state->page_cache.entries[0].file_pos = (uint32_t)saved_offset;
        state->page_cache.entries[0].state = RENDER_STATE_DEFAULT;
        state->page_cache.entries[0].screen_width = 0;
        state->page_cache.entries[0].content_rows = 0;
    }

    reader_toc_load_total_pages(state);

    return 1;
}

int reader_load_current_page(reader_state_t *state, int *bold_pending, int *underline_pending) {
    if (!state->file) {
        return 0;
    }

    if (bold_pending) {
        *bold_pending = 0;
    }
    if (underline_pending) {
        *underline_pending = 0;
    }

    uint32_t offset = state->page_cache.entries[state->page_cache.current].file_pos;
    int cache_index = state->page_cache.current;

    printf("LOAD_PAGE: Page %d, seeking to offset %u (cache index %d)\n", state->page_number, offset, cache_index);

    // Use the new renderer-based page loading
    page_renderer_t renderer;
    render_state_t cached_state = RENDER_STATE_DEFAULT;
    uint8_t heading_levels[MAX_RENDERED_LINES];  // Temporary array for heading levels

    if (cache_index >= 0 && cache_index < state->page_cache.count) {
        cached_state = state->page_cache.entries[cache_index].state;
        printf("LOAD_PAGE: Continuing in state %d\n", cached_state);
    } else {
        printf("LOAD_PAGE: Starting fresh\n");
    }

    if (state->pipeline.file_mutex) {
        os_semaphore_take(state->pipeline.file_mutex, -1);
    }
    fseek(state->file, offset, SEEK_SET);
    renderer_init(&renderer, state->file, state->lines, heading_levels, state->content_rows, state->screen_width, state->line_buf_size);
    renderer.state = cached_state;
    renderer.tokenizer.current_pos = offset;

    int64_t t0 = esp_timer_get_time();
    renderer_process_page(&renderer);
    int64_t t1 = esp_timer_get_time();
    if (state->pipeline.file_mutex) {
        os_semaphore_give(state->pipeline.file_mutex);
    }
    state->line_count = renderer.line_count;
    printf("LOAD_PAGE: render took %lld us for %d lines\n", t1 - t0, state->line_count);

    // Use the saved next page position from the renderer, not ftell()
    long actual_position = renderer_get_position(&renderer);
    if (state->pipeline.file_mutex) {
        os_semaphore_take(state->pipeline.file_mutex, -1);
    }
    long ftell_pos = ftell(state->file);
    if (state->pipeline.file_mutex) {
        os_semaphore_give(state->pipeline.file_mutex);
    }
    printf("LOAD_PAGE: Got %d lines, renderer says next page starts at: %ld (ftell says: %ld)\n",
           state->line_count, actual_position, ftell_pos);

    // Sanity check: position should never be less than the starting position
    long start_position = state->page_cache.entries[state->page_cache.current].file_pos;
    if (actual_position < start_position) {
        printf("LOAD_PAGE: ERROR! Position went backwards from %ld to %ld\n",
               start_position, actual_position);
        actual_position = start_position; // Don't go backwards
    }

    // Fix current cache entry dimensions (may be stale from book open)
    state->page_cache.entries[state->page_cache.current].screen_width = state->screen_width;
    state->page_cache.entries[state->page_cache.current].content_rows = state->content_rows;

    // Update the NEXT cache entry with the correct position (for forward navigation)
    if (state->page_cache.current + 1 < PAGE_CACHE_ENTRIES) {
        // Normal case: store at the next slot
        state->page_cache.entries[state->page_cache.current + 1].file_pos = actual_position;
        state->page_cache.entries[state->page_cache.current + 1].state = RENDER_STATE_DEFAULT;
        state->page_cache.entries[state->page_cache.current + 1].screen_width = state->screen_width;
        state->page_cache.entries[state->page_cache.current + 1].content_rows = state->content_rows;
        if (state->page_cache.current + 1 >= state->page_cache.count) {
            state->page_cache.count = state->page_cache.current + 2;
        }
        printf("LOAD_PAGE: Set NEXT cache entry %d to position %ld\n",
               state->page_cache.current + 1, actual_position);
    } else {
        // Ring buffer full: shift entries[1..15] → entries[0..14]
        // to make room for the next page position at entries[15]
        for (int i = 0; i < PAGE_CACHE_ENTRIES - 1; i++) {
            state->page_cache.entries[i] = state->page_cache.entries[i + 1];
        }
        state->page_cache.entries[PAGE_CACHE_ENTRIES - 1].file_pos = actual_position;
        state->page_cache.entries[PAGE_CACHE_ENTRIES - 1].state = RENDER_STATE_DEFAULT;
        state->page_cache.entries[PAGE_CACHE_ENTRIES - 1].screen_width = state->screen_width;
        state->page_cache.entries[PAGE_CACHE_ENTRIES - 1].content_rows = state->content_rows;
        // Adjust current to keep pointing at the same rendered page
        // (it shifted one position left)
        state->page_cache.current--;
        if (state->page_cache.count < PAGE_CACHE_ENTRIES) {
            state->page_cache.count = PAGE_CACHE_ENTRIES;
        }
        printf("LOAD_PAGE: Ring buffer shifted, next page at %ld, current now %d\n",
               actual_position, state->page_cache.current);
    }

    return state->line_count;
}

bool reader_alloc_lines(reader_state_t *state, int screen_width, int content_rows) {
    int needed = screen_width + LINE_BUF_MARGIN;
    if (state->lines && state->line_buf_size == needed && content_rows <= MAX_RENDERED_LINES) {
        return true;
    }

    reader_free_lines(state);

    // When the pipeline is active, point state->lines at the display buffer
    if (state->pipeline.task) {
        if (!render_pipeline_ensure_buffers(&state->pipeline, screen_width, content_rows)) {
            return false;
        }
        state->lines = state->pipeline.buffers[state->pipeline.display_buffer].lines;
        state->line_buf_size = needed;
        return true;
    }

    state->lines = calloc(MAX_RENDERED_LINES, sizeof(rendered_line_t));
    if (!state->lines) return false;

    char *text_buf = malloc((size_t)MAX_RENDERED_LINES * needed);
    if (!text_buf) {
        free(state->lines);
        state->lines = NULL;
        return false;
    }

    for (int i = 0; i < MAX_RENDERED_LINES; i++) {
        state->lines[i].text = text_buf + (size_t)i * needed;
        state->lines[i].text[0] = '\0';
        state->lines[i].color = TEXT_COLOR_WHITE;
        state->lines[i].attr = TEXT_ATTR_NORMAL;
    }

    state->line_buf_size = needed;
    return true;
}

void reader_free_lines(reader_state_t *state) {
    // Only free if state->lines is NOT a pipeline buffer (pipeline manages its own)
    if (state->lines && !state->pipeline.task) {
        if (state->lines[0].text) {
            free(state->lines[0].text);
        }
        free(state->lines);
    }
    state->lines = NULL;
    state->line_buf_size = 0;
}
