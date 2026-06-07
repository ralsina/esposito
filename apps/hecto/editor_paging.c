#include "editor_state.h"
#include "os_core.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "editor2_paging";

void page_cache_init(page_cache_t *cache) {
    memset(cache, 0, sizeof(page_cache_t));
}

bool page_cache_is_valid(page_cache_t *cache, int screen_width, int content_rows) {
    if (cache->count == 0 || cache->current < 0 || cache->current >= cache->count) {
        return false;
    }

    page_cache_entry_t *entry = &cache->entries[cache->current];
    return entry->screen_width == screen_width &&
           entry->content_rows == content_rows;
}

long page_cache_current_pos(page_cache_t *cache) {
    if (cache->count == 0 || cache->current < 0 || cache->current >= cache->count) {
        return -1;
    }
    return cache->entries[cache->current].file_pos;
}

// Navigate to next page in cache
bool page_cache_can_next(page_cache_t *cache) {
    return cache->count > 0 && cache->current < cache->count - 1;
}

long page_cache_next(page_cache_t *cache) {
    if (!page_cache_can_next(cache)) {
        return -1;
    }
    cache->current++;
    return cache->entries[cache->current].file_pos;
}

// Navigate to previous page in cache
bool page_cache_can_prev(page_cache_t *cache) {
    return cache->count > 0 && cache->current > 0;
}

long page_cache_prev(page_cache_t *cache) {
    if (!page_cache_can_prev(cache)) {
        return -1;
    }
    cache->current--;
    return cache->entries[cache->current].file_pos;
}

// Add entry to page cache
bool page_cache_add(page_cache_t *cache, long file_pos, int screen_width, int content_rows) {
    if (cache->count >= 16) {
        return false; // Cache full
    }

    page_cache_entry_t *entry = &cache->entries[cache->count];
    entry->file_pos = file_pos;
    entry->screen_width = screen_width;
    entry->content_rows = content_rows;
    entry->state = 0;

    cache->count++;
    return true;
}

// Estimate file position for next page
long estimate_next_page_offset(FILE *file, long current_offset, int lines_per_page) {
    if (!file) return -1;

    fseek(file, current_offset, SEEK_SET);

    char buffer[512];
    int lines = 0;
    long offset = current_offset;

    while (fgets(buffer, sizeof(buffer), file) && lines < lines_per_page) {
        offset = ftell(file);
        lines++;
    }

    return offset;
}

// Calculate total pages in file
int calculate_total_pages(FILE *file, int lines_per_page) {
    if (!file) return 1;

    long original_pos = ftell(file);
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Count lines
    int total_lines = 0;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), file)) {
        total_lines++;
    }

    fseek(file, original_pos, SEEK_SET);

    int pages = (total_lines / lines_per_page) + 1;
    return pages > 0 ? pages : 1;
}