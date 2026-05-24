#include "reader_page.h"

void page_cache_init(page_cache_t *cache) {
    cache->count = 0;
    cache->current = -1;
    for (int i = 0; i < PAGE_CACHE_ENTRIES; i++) {
        cache->entries[i].file_offset = 0;
        cache->entries[i].screen_width = 0;
        cache->entries[i].content_rows = 0;
    }
}

void page_cache_set_start(page_cache_t *cache, uint32_t offset) {
    cache->entries[0].file_offset = offset;
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
    return cache->entries[cache->current].file_offset;
}

uint32_t page_cache_next(page_cache_t *cache) {
    if (cache->current < cache->count - 1) {
        cache->current++;
    }
    return cache->entries[cache->current].file_offset;
}

void page_cache_add_next(page_cache_t *cache, uint32_t offset) {
    int next = cache->current + 1;
    if (next < PAGE_CACHE_ENTRIES) {
        cache->entries[next].file_offset = offset;
        if (next + 1 > cache->count) cache->count = next + 1;
    } else {
        // Ring buffer full: drop oldest entry, shift left, put new at end
        for (int i = 0; i < PAGE_CACHE_ENTRIES - 1; i++) {
            cache->entries[i] = cache->entries[i + 1];
        }
        cache->entries[PAGE_CACHE_ENTRIES - 1].file_offset = offset;
        cache->current = PAGE_CACHE_ENTRIES - 1;
    }
}

uint32_t page_cache_current_offset(page_cache_t *cache) {
    return cache->entries[cache->current].file_offset;
}

int page_cache_page_number(page_cache_t *cache) {
    return cache->current + 1;
}

bool page_cache_is_valid(page_cache_t *cache, int current_screen_width, int current_content_rows) {
    if (cache->count == 0) return false;
    int current = cache->current;
    if (current < 0 || current >= cache->count) return false;

    // Cache is invalid if screen dimensions have changed for the current entry
    page_cache_entry_t *entry = &cache->entries[current];
    if (entry->screen_width != current_screen_width || entry->content_rows != current_content_rows) {
        return false;
    }
    return true;
}

md_parser_state_t* page_cache_get_parser_state(page_cache_t *cache, int entry_index) {
    if (entry_index < 0 || entry_index >= cache->count) {
        return NULL;
    }
    return &cache->entries[entry_index].parser_state;
}

void page_cache_set_parser_state(page_cache_t *cache, int entry_index) {
    if (entry_index < 0 || entry_index >= cache->count) {
        return;
    }
    md_get_parser_state(&cache->entries[entry_index].parser_state);
}
