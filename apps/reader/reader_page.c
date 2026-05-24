#include "reader_page.h"

void page_cache_init(page_cache_t *cache) {
    cache->count = 0;
    cache->current = -1;
    cache->screen_width = 0;
    cache->content_rows = 0;
}

void page_cache_set_start(page_cache_t *cache, uint32_t offset) {
    cache->offsets[0] = offset;
    cache->count = 1;
    cache->current = 0;
}

bool page_cache_can_prev(page_cache_t *cache) {
    return cache->current > 0;
}

bool page_cache_can_next(page_cache_t *cache) {
    return cache->current < cache->count - 1;
}

uint32_t page_cache_prev(page_cache_t *cache) {
    if (cache->current > 0) {
        cache->current--;
    }
    return cache->offsets[cache->current];
}

uint32_t page_cache_next(page_cache_t *cache) {
    if (cache->current < cache->count - 1) {
        cache->current++;
    }
    return cache->offsets[cache->current];
}

void page_cache_add_next(page_cache_t *cache, uint32_t offset) {
    int next = cache->current + 1;
    if (next < PAGE_CACHE_ENTRIES) {
        cache->offsets[next] = offset;
        if (next + 1 > cache->count) cache->count = next + 1;
    } else {
        // Ring buffer full: drop oldest entry, shift left, put new at end
        for (int i = 0; i < PAGE_CACHE_ENTRIES - 1; i++) {
            cache->offsets[i] = cache->offsets[i + 1];
        }
        cache->offsets[PAGE_CACHE_ENTRIES - 1] = offset;
        cache->current = PAGE_CACHE_ENTRIES - 1;
    }
}

uint32_t page_cache_current_offset(page_cache_t *cache) {
    return cache->offsets[cache->current];
}

int page_cache_page_number(page_cache_t *cache) {
    return cache->current + 1;
}

bool page_cache_is_valid(page_cache_t *cache, int current_screen_width, int current_content_rows) {
    // Cache is invalid if screen dimensions have changed
    if (cache->screen_width != current_screen_width || cache->content_rows != current_content_rows) {
        return false;
    }
    return cache->count > 0;
}
