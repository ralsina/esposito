#include "reader_page.h"

void page_cache_init(page_cache_t *cache) {
    cache->count = 0;
    cache->current = -1;
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
    if (cache->current < PAGE_CACHE_ENTRIES - 1) {
        cache->offsets[cache->current + 1] = offset;
        cache->count = cache->current + 2;
    }
}

uint32_t page_cache_current_offset(page_cache_t *cache) {
    return cache->offsets[cache->current];
}

int page_cache_page_number(page_cache_t *cache) {
    return cache->current + 1;
}
