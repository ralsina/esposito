#ifndef READER_PAGE_H
#define READER_PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "reader_md.h"

#define PAGE_CACHE_ENTRIES 16

typedef struct {
    uint32_t file_offset;          // File position to seek to
    md_parser_state_t parser_state; // Complete parser state for this page
    int screen_width;              // Screen dimensions when cached
    int content_rows;
} page_cache_entry_t;

typedef struct {
    page_cache_entry_t entries[PAGE_CACHE_ENTRIES];
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
bool page_cache_is_valid(page_cache_t *cache, int current_screen_width, int current_content_rows);
md_parser_state_t* page_cache_get_parser_state(page_cache_t *cache, int entry_index);
void page_cache_set_parser_state(page_cache_t *cache, int entry_index);

#endif
