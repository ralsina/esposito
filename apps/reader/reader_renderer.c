#include "reader_renderer.h"
#include <text_mode.h>
#include <string.h>
#include <stdio.h>

void renderer_init(page_renderer_t *renderer, FILE *file, rendered_line_t *lines, uint8_t *heading_levels, int max_lines, int screen_width) {
    tokenizer_init(&renderer->tokenizer, file);
    renderer->state = RENDER_STATE_DEFAULT;
    renderer->current_line = 0;
    renderer->max_lines = max_lines;
    renderer->screen_width = screen_width;
    renderer->lines = lines;
    renderer->heading_levels = heading_levels;
    renderer->line_count = 0;
    renderer->in_paragraph = false;
    renderer->page_full = false;
    renderer->needs_blank_line = false;
}

bool renderer_process_page(page_renderer_t *renderer) {
    if (!renderer || !renderer->tokenizer.file) {
        return false;
    }

    renderer->current_line = 0;
    renderer->line_count = 0;
    renderer->page_full = false;
    renderer->needs_blank_line = false;

    // Clear all lines
    for (int i = 0; i < renderer->max_lines; i++) {
        renderer->lines[i].text[0] = '\0';
        renderer->lines[i].color = TEXT_COLOR_WHITE;  // Default text color
        renderer->lines[i].attr = TEXT_ATTR_NORMAL;   // Default attributes
        if (renderer->heading_levels) {
            renderer->heading_levels[i] = 0;
        }
    }

    printf("RENDER: Starting page render, max_lines=%d screen_width=%d\n", renderer->max_lines, renderer->screen_width);

    while (!renderer->page_full && tokenizer_next(&renderer->tokenizer)) {
        token_t *token = tokenizer_current(&renderer->tokenizer);
        if (!token) break;

        //printf("RENDER: Token type=%d text='%s' line=%d\n", token->type, token->text, renderer->current_line);

        switch (token->type) {
            case TOKEN_HASH:
                // Handle heading markers
                if (renderer->state == RENDER_STATE_DEFAULT) {
                    int hash_count = 1;
                    while (tokenizer_next(&renderer->tokenizer)) {
                        token_t *next = tokenizer_current(&renderer->tokenizer);
                        if (next && next->type == TOKEN_HASH) {
                            hash_count++;
                        } else {
                            // Put back the non-hash token
                            if (next) {
                                fseek(renderer->tokenizer.file, next->file_pos, SEEK_SET);
                                renderer->tokenizer.current_pos = next->file_pos;
                            }
                            break;
                        }
                    }
                    // Set heading state
                    if (hash_count >= 1 && hash_count <= 6) {
                        renderer->state = RENDER_STATE_HEADING_1 + (hash_count - 1);
                    }
                }
                break;

            case TOKEN_NEWLINE:
                if (renderer->state != RENDER_STATE_DEFAULT) {
                    // End of heading - hard break
                    if (strlen(renderer->lines[renderer->current_line].text) > 0) {
                        if (renderer->current_line + 1 >= renderer->max_lines) {
                            renderer->page_full = true;
                            goto page_done;
                        }
                        renderer->current_line++;
                        renderer->line_count = renderer->current_line + 1;
                    }
                    renderer->state = RENDER_STATE_DEFAULT;
                    renderer->in_paragraph = false;
                    renderer->needs_blank_line = true;
                } else if (renderer->in_paragraph) {
                    // Single newline within paragraph = soft break (space)
                    int len = strlen(renderer->lines[renderer->current_line].text);
                    if (len > 0 && len + 1 < (int)sizeof(renderer->lines[renderer->current_line].text)) {
                        renderer->lines[renderer->current_line].text[len] = ' ';
                        renderer->lines[renderer->current_line].text[len + 1] = '\0';
                    }
                    renderer->in_paragraph = false;
                } else {
                    // Blank line(s) = paragraph break
                    if (strlen(renderer->lines[renderer->current_line].text) > 0) {
                        if (renderer->current_line + 1 >= renderer->max_lines) {
                            renderer->page_full = true;
                            goto page_done;
                        }
                        renderer->current_line++;
                        renderer->line_count = renderer->current_line + 1;
                        renderer->needs_blank_line = true;
                    }
                    // If current line is empty: collapse consecutive blank lines
                    // needs_blank_line flag (if set) remains, so next content skips past the reserved blank line
                }
                break;

            case TOKEN_SPACE:
                // Just add space to current line, skip leading spaces
                if (renderer->current_line < renderer->max_lines) {
                    int len = strlen(renderer->lines[renderer->current_line].text);
                    if (len > 0 && len < sizeof(renderer->lines[renderer->current_line].text) - 1) {
                        renderer->lines[renderer->current_line].text[len] = ' ';
                        renderer->lines[renderer->current_line].text[len + 1] = '\0';
                    }
                }
                break;

            case TOKEN_WORD:
                // If we have a pending paragraph break, skip past the reserved blank line
                if (renderer->needs_blank_line) {
                    if (strlen(renderer->lines[renderer->current_line].text) == 0) {
                        if (renderer->current_line + 1 >= renderer->max_lines) {
                            renderer->page_full = true;
                            fseek(renderer->tokenizer.file, token->file_pos, SEEK_SET);
                            renderer->tokenizer.current_pos = token->file_pos;
                            renderer->tokenizer.next_page_start = renderer->tokenizer.current_pos;
                            goto page_done;
                        }
                        renderer->current_line++;
                        renderer->line_count = renderer->current_line + 1;
                    }
                    renderer->needs_blank_line = false;
                }
                // Add word to current line with wrapping
                {
                    int word_len = token->text_len;
                    int line_len = strlen(renderer->lines[renderer->current_line].text);

                    // Check if word fits on current line
                    if (line_len + word_len > renderer->screen_width && line_len > 0) {
                        // Need to wrap to next line
                        renderer->current_line++;
                        if (renderer->current_line >= renderer->max_lines) {
                            renderer->page_full = true;
                            printf("RENDER: Page full at line %d (max_lines=%d)\n", renderer->current_line, renderer->max_lines);
                            // Put the token back for next page (seek back by token length)
                            fseek(renderer->tokenizer.file, token->file_pos, SEEK_SET);
                            renderer->tokenizer.current_pos = token->file_pos;
                            // Next page starts from AFTER the seek back (current position)
                            renderer->tokenizer.next_page_start = renderer->tokenizer.current_pos;
                            printf("RENDER: Next page starts at %ld (put back token of length %d)\n",
                                   renderer->tokenizer.next_page_start, token->text_len);
                            goto page_done;  // Exit both switch and while loop
                        }
                        line_len = 0;
                    }

                    // Add word to line, handling markdown formatting
                    if (renderer->current_line < renderer->max_lines) {
                        if (line_len + word_len < sizeof(renderer->lines[renderer->current_line].text)) {
                            size_t wi = 0;
                            for (size_t ri = 0; ri < word_len; ri++) {
                                char c = token->text[ri];

                                // Markdown image: ![alt](url) → strip entirely
                                if (c == '!' && ri + 1 < word_len && token->text[ri + 1] == '[') {
                                    size_t end_bracket = ri + 2;
                                    while (end_bracket < word_len && token->text[end_bracket] != ']') end_bracket++;
                                    if (end_bracket < word_len && end_bracket + 1 < word_len && token->text[end_bracket + 1] == '(') {
                                        size_t end_paren = end_bracket + 2;
                                        while (end_paren < word_len && token->text[end_paren] != ')') end_paren++;
                                        if (end_paren < word_len) {
                                            ri = end_paren;
                                            continue;
                                        }
                                    }
                                    renderer->lines[renderer->current_line].text[line_len + wi++] = c;
                                    continue;
                                }

                                // Markdown link: [text](url) → underlined text
                                if (c == '[') {
                                    size_t end_bracket = ri + 1;
                                    while (end_bracket < word_len && token->text[end_bracket] != ']') end_bracket++;
                                    if (end_bracket < word_len && end_bracket + 1 < word_len && token->text[end_bracket + 1] == '(') {
                                        size_t end_paren = end_bracket + 2;
                                        while (end_paren < word_len && token->text[end_paren] != ')') end_paren++;
                                        if (end_paren < word_len) {
                                            renderer->lines[renderer->current_line].text[line_len + wi++] = MD_FORMAT_UNDERLINE;
                                            for (size_t j = ri + 1; j < end_bracket; j++) {
                                                renderer->lines[renderer->current_line].text[line_len + wi++] = token->text[j];
                                            }
                                            renderer->lines[renderer->current_line].text[line_len + wi++] = MD_FORMAT_UNDERLINE;
                                            ri = end_paren;
                                            continue;
                                        }
                                    }
                                    renderer->lines[renderer->current_line].text[line_len + wi++] = c;
                                    continue;
                                }

                                if (c == '*') {
                                    if (ri + 1 < word_len && token->text[ri + 1] == '*') {
                                        renderer->lines[renderer->current_line].text[line_len + wi++] = MD_FORMAT_BOLD;
                                        ri++; // skip the second *
                                    } else {
                                        renderer->lines[renderer->current_line].text[line_len + wi++] = MD_FORMAT_TOGGLE;
                                    }
                                    continue;
                                }

                                renderer->lines[renderer->current_line].text[line_len + wi++] = c;
                            }
                            int new_word_len = (int)wi;
                            renderer->lines[renderer->current_line].text[line_len + new_word_len] = '\0';

                            // Set default color and attributes for this line (first time we add content)
                            if (line_len == 0) {
                                renderer->lines[renderer->current_line].color = TEXT_COLOR_WHITE;
                                renderer->lines[renderer->current_line].attr = TEXT_ATTR_NORMAL;
                            }

                            // Set heading level for this line
                            if (renderer->state >= RENDER_STATE_HEADING_1 && renderer->state <= RENDER_STATE_HEADING_6) {
                                if (renderer->heading_levels) {
                                    renderer->heading_levels[renderer->current_line] = renderer->state - RENDER_STATE_HEADING_1 + 1;
                                }
                                // Make headings bold and cyan
                                renderer->lines[renderer->current_line].attr = TEXT_ATTR_BOLD;
                                renderer->lines[renderer->current_line].color = TEXT_COLOR_CYAN;
                            }
                        }
                    }

                    renderer->in_paragraph = true;
                    // Only update line_count when we actually add content to a line
                    if (line_len + word_len > 0) {
                        renderer->line_count = renderer->current_line + 1;
                    }
                }
                break;

            case TOKEN_EOF:
                renderer->page_full = true;
                break;
        }
    }

page_done:

    printf("RENDER: Done - line_count=%d page_full=%d\n", renderer->line_count, renderer->page_full);

    return renderer->line_count > 0;
}

long renderer_get_position(page_renderer_t *renderer) {
    if (!renderer) {
        return 0;
    }
    // Return the saved next page start position if available, otherwise current position
    if (renderer->tokenizer.next_page_start > 0) {
        return renderer->tokenizer.next_page_start;
    }
    return tokenizer_get_position(&renderer->tokenizer);
}

render_state_t renderer_get_state(page_renderer_t *renderer) {
    if (!renderer) {
        return RENDER_STATE_DEFAULT;
    }
    return renderer->state;
}

void renderer_set_position(page_renderer_t *renderer, long file_pos, render_state_t state, int screen_width, int content_rows) {
    if (!renderer || !renderer->tokenizer.file) {
        return;
    }

    tokenizer_set_position(&renderer->tokenizer, file_pos);
    renderer->state = state;
    renderer->screen_width = screen_width;
    renderer->max_lines = content_rows;
    renderer->current_line = 0;
    renderer->line_count = 0;
    renderer->in_paragraph = false;
    renderer->page_full = false;
    renderer->needs_blank_line = false;
}

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