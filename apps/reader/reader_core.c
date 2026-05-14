#include "reader_core.h"

#include "checkpoint.h"
#include "reader_md.h"
#include "reader_toc.h"

#include <dirent.h>
#include <stdint.h>
#include <string.h>

void reader_build_book_key(char *out, size_t out_size, const char *prefix, const char *path) {
    snprintf(out, out_size, "%s:%s", prefix, path);
}

void reader_save_current_book_progress(reader_state_t *state) {
    if (!state->file || !state->current_file[0] || state->page_cache.current < 0) {
        return;
    }

    char offset_key[320];
    char page_key[320];
    reader_build_book_key(offset_key, sizeof(offset_key), KEY_BOOK_OFFSET_PREFIX, state->current_file);
    reader_build_book_key(page_key, sizeof(page_key), KEY_BOOK_PAGE_PREFIX, state->current_file);

    checkpoint_save_int(offset_key, (int)page_cache_current_offset(&state->page_cache));
    checkpoint_save_int(page_key, state->page_number);
    checkpoint_save_string(KEY_LAST_FILE, state->current_file);
}

void reader_scan_md_files(reader_state_t *state) {
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

        strncpy(state->file_names[state->file_count], entry->d_name, 63);
        state->file_names[state->file_count][63] = '\0';
        snprintf(state->file_paths[state->file_count], MAX_PATH, "/sdcard/books/%s", entry->d_name);
        state->file_ptrs[state->file_count] = state->file_names[state->file_count];
        state->file_count++;
    }

    closedir(dir);
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
        reader_save_current_book_progress(state);
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
    page_cache_set_start(&state->page_cache, 0);
    state->page_number = 1;

    char offset_key[320];
    char page_key[320];
    reader_build_book_key(offset_key, sizeof(offset_key), KEY_BOOK_OFFSET_PREFIX, path);
    reader_build_book_key(page_key, sizeof(page_key), KEY_BOOK_PAGE_PREFIX, path);

    int saved_offset = checkpoint_load_int(offset_key);
    int saved_page = checkpoint_load_int(page_key);

    if (saved_offset > 0) {
        page_cache_set_start(&state->page_cache, (uint32_t)saved_offset);
    }
    if (saved_page > 0) {
        state->page_number = saved_page;
    }

    reader_toc_load_or_build(state);

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

    uint32_t offset = page_cache_current_offset(&state->page_cache);
    fseek(state->file, offset, SEEK_SET);
    state->line_count = md_scan_page(state->file, state->lines, state->content_rows, state->screen_width);
    return state->line_count;
}
