#include "reader_core.h"

#include "app_config.h"
#include "reader_md.h"
#include "reader_toc.h"

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

void reader_save_current_book_progress(reader_state_t *state) {
    if (!state->file || !state->current_file[0] || state->page_cache.current < 0) {
        return;
    }

    char offset_key[320];
    char page_key[320];
    reader_build_book_key(offset_key, sizeof(offset_key), KEY_BOOK_OFFSET_PREFIX, state->current_file);
    reader_build_book_key(page_key, sizeof(page_key), KEY_BOOK_PAGE_PREFIX, state->current_file);

    config_set_int(offset_key, (int)page_cache_current_offset(&state->page_cache));
    config_set_int(page_key, state->page_number);
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

    int saved_offset = config_get_int(offset_key, 0);
    int saved_page = config_get_int(page_key, 0);

    if (saved_offset > 0) {
        page_cache_set_start(&state->page_cache, (uint32_t)saved_offset);
    }
    if (saved_page > 0) {
        state->page_number = saved_page;
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

    uint32_t offset = page_cache_current_offset(&state->page_cache);
    fseek(state->file, offset, SEEK_SET);
    state->line_count = md_scan_page(state->file, state->lines, state->content_rows, state->screen_width);
    return state->line_count;
}
