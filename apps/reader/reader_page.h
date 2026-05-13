#ifndef READER_PAGE_H
#define READER_PAGE_H

#include <stdint.h>
#include <stdbool.h>

#define PAGE_CACHE_ENTRIES 16

typedef struct {
    uint32_t offsets[PAGE_CACHE_ENTRIES];
    int count;
    int current;
} page_cache_t;

void page_cache_init(page_cache_t *cache);
void page_cache_set_start(page_cache_t *cache, uint32_t offset);
bool page_cache_can_prev(page_cache_t *cache);
bool page_cache_can_next(page_cache_t *cache);
uint32_t page_cache_prev(page_cache_t *cache);
uint32_t page_cache_next(page_cache_t *cache);
void page_cache_add_next(page_cache_t *cache, uint32_t offset);
uint32_t page_cache_current_offset(page_cache_t *cache);
int page_cache_page_number(page_cache_t *cache);

#endif
