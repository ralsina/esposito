#include "reader_renderer.h"

#include <string.h>

void page_cache_init(page_cache_t *cache) {
    cache->count = 0;
    cache->current = -1;
    for (int i = 0; i < 16; i++) {
        cache->entries[i].file_pos = 0;
        cache->entries[i].state = RENDER_STATE_DEFAULT;
        cache->entries[i].screen_width = 0;
        cache->entries[i].content_rows = 0;
    }
}

bool page_cache_add(page_cache_t *cache, long file_pos, render_state_t state, int screen_width, int content_rows) {
    int next = cache->current + 1;
    if (next < 16) {
        cache->entries[next].file_pos = file_pos;
        cache->entries[next].state = state;
        cache->entries[next].screen_width = screen_width;
        cache->entries[next].content_rows = content_rows;
        if (next + 1 > cache->count) cache->count = next + 1;
        return true;
    } else {
        // Ring buffer full - drop oldest entry
        for (int i = 0; i < 15; i++) {
            cache->entries[i] = cache->entries[i + 1];
        }
        cache->entries[15].file_pos = file_pos;
        cache->entries[15].state = state;
        cache->entries[15].screen_width = screen_width;
        cache->entries[15].content_rows = content_rows;
        cache->current = 15;
        return true;
    }
}

bool page_cache_can_prev(page_cache_t *cache) {
    return cache->current > 0;
}

bool page_cache_can_next(page_cache_t *cache) {
    return cache->current < cache->count - 1;
}

long page_cache_prev(page_cache_t *cache) {
    if (cache->current > 0) {
        cache->current--;
    }
    return cache->entries[cache->current].file_pos;
}

long page_cache_next(page_cache_t *cache) {
    if (cache->current < cache->count - 1) {
        cache->current++;
    }
    return cache->entries[cache->current].file_pos;
}

page_cache_entry_t* page_cache_current(page_cache_t *cache) {
    if (cache->current < 0 || cache->current >= cache->count) {
        return NULL;
    }
    return &cache->entries[cache->current];
}

bool page_cache_is_valid(page_cache_t *cache, int screen_width, int content_rows) {
    if (cache->count == 0 || cache->current < 0 || cache->current >= cache->count) {
        return false;
    }

    page_cache_entry_t *entry = &cache->entries[cache->current];
    return entry->screen_width == screen_width && entry->content_rows == content_rows;
}