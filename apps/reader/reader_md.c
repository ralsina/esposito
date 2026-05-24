#include "reader_md.h"
#include "text_mode.h"
#include <string.h>
#include <stdio.h>

#define PARA_BUF_SIZE 4096

typedef struct {
    char text[PARA_BUF_SIZE];
    int type;
    int heading_level;
    long start_pos;
} markdown_block_t;

static char para_remainder[PARA_BUF_SIZE];
static int has_remainder = 0;
static int remainder_para_type = 0;
static int remainder_heading_level = 0;
static int carry_spacer = 0;
static int in_tag = 0;

// Current parser state (for page caching)
static md_parser_state_enum_t current_parser_state = MD_STATE_DEFAULT;

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_blank_line(const char *line) {
    while (*line) {
        if (!is_space((unsigned char)*line)) return 0;
        line++;
    }
    return 1;
}

static int is_heading_line(const char *line) {
    return line[0] == '#';
}

static int is_tag_only_line(const char *line) {
    while (*line) {
        if (*line == '<') {
            line++;
            while (*line && *line != '>') line++;
            if (!*line) return 0;
            line++;
            while (*line == ' ' || *line == '\t') line++;
            continue;
        }
        if (*line == ' ' || *line == '\t') {
            line++;
            continue;
        }
        return 0;
    }
    return 1;
}

static int is_hr_line(const char *line) {
    int count = 0;
    while (*line == '-') { count++; line++; }
    while (*line == ' ' || *line == '\t') line++;
    return count >= 4 && *line == '\0';
}

static void strip_newline(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

static void strip_html(char *line) {
    char *dst = line;
    unsigned char *src = (unsigned char *)line;
    if (in_tag) {
        while (*src && *src != '>') src++;
        if (*src == '>') { src++; in_tag = 0; }
        dst = (char *)src;
    }
    while (*src) {
        if (*src == '<') {
            src++;
            while (*src && *src != '>') src++;
            if (*src == '>') { src++; continue; }
            in_tag = 1;
            *dst = '\0';
            return;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void asciify(char *line) {
    char *dst = line;
    unsigned char *src = (unsigned char *)line;
    while (*src) {
        if (*src == 0xE2) {
            if (src[1] == 0x80) {
                switch (src[2]) {
                    case 0x9C: *dst = '"'; src += 3; dst++; continue;
                    case 0x9D: *dst = '"'; src += 3; dst++; continue;
                    case 0x98: *dst = '\''; src += 3; dst++; continue;
                    case 0x99: *dst = '\''; src += 3; dst++; continue;
                    case 0x93: *dst = '-'; src += 3; dst++; continue;
                    case 0x94: *dst = '-'; src += 3; dst++; continue;
                    case 0xA6: *dst = '.'; src += 3; dst++;
                               *dst++ = '.'; *dst++ = '.';
                               src += 3;
                               continue;
                    default: break;
                }
            }
            src++;
            if (*src) src++;
            if (*src) src++;
            continue;
        }
        if (*src == '\\') {
            src++;
            if (*src) { *dst++ = *src++; continue; }
            break;
        }
        if (*src < 0x20 || *src > 0x7E) {
            src++;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void strip_markdown_images(char *line) {
    char *cursor = line;
    while (*cursor) {
        if (cursor[0] == '!' && cursor[1] == '[') {
            char *alt_end = strchr(cursor + 2, ']');
            if (alt_end && alt_end[1] == '(') {
                char *url_end = strchr(alt_end + 2, ')');
                if (url_end) {
                    memmove(cursor, url_end + 1, strlen(url_end + 1) + 1);
                    continue;
                }
            }
        }
        cursor++;
    }
}

static void convert_markdown_links(char *line) {
    char *src = line;
    char *dst = line;

    while (*src) {
        if (src[0] == '[') {
            char *text_end = strchr(src + 1, ']');
            if (text_end && text_end[1] == '(') {
                char *url_end = strchr(text_end + 2, ')');
                if (url_end) {
                    // Emit link text underlined by wrapping each word with toggles.
                    char *text = src + 1;
                    while (text < text_end) {
                        while (text < text_end && is_space((unsigned char)*text)) {
                            *dst++ = *text++;
                        }
                        if (text >= text_end) break;

                        *dst++ = MD_FORMAT_TOGGLE;
                        while (text < text_end && !is_space((unsigned char)*text)) {
                            *dst++ = *text++;
                        }
                        *dst++ = MD_FORMAT_TOGGLE;
                    }

                    src = url_end + 1;
                    continue;
                }
            }
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

static const char *find_emphasis_close(const char *text) {
    while (*text) {
        if (text[0] == '*' && text[1] == '*') {
            text += 2;
            continue;
        }
        if (*text == '*') {
            return text;
        }
        text++;
    }
    return NULL;
}

static void convert_markdown_emphasis(char *line) {
    char *src = line;
    char *dst = line;

    while (*src) {
        if (src[0] == '*' && src[1] != '*') {
            const char *close = find_emphasis_close(src + 1);
            if (close) {
                const char *inner = src + 1;
                *dst++ = MD_FORMAT_TOGGLE;
                while (inner < close) {
                    *dst++ = *inner++;
                }
                *dst++ = MD_FORMAT_TOGGLE;
                src = (char *)close + 1;
                continue;
            }
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

static void normalize_markdown_text(char *line) {
    strip_html(line);
    asciify(line);
    strip_markdown_images(line);
    convert_markdown_emphasis(line);
    convert_markdown_links(line);
}

static int wrap_line(const char *text, int width, char *out, int max_out) {
    const char *original = text;
    while (*text == ' ') text++;
    if (!*text) return 0;

    const char *start = text;
    int remaining = width;
    int written = 0;

    while (*text && remaining > 0) {
        const char *word_start = text;
        while (*text && *text != ' ') text++;
        int word_chars = 0;
        const char *probe = word_start;
        while (probe < text) {
            if (*probe != MD_FORMAT_TOGGLE) word_chars++;
            probe++;
        }

        // Account for the separator space that will be emitted between words.
        if ((word_chars + 1) > remaining && written > 0) {
            text = word_start;
            break;
        }

        int split_long_word = (word_chars > width);
        if (split_long_word) {
            int visible = 0;
            const char *split = word_start;
            while (*split && *split != ' ' && visible < width) {
                if (*split != MD_FORMAT_TOGGLE) visible++;
                split++;
            }
            text = split;
        }

        if (written > 0) {
            if (written < max_out) *out++ = ' ';
            written++;
            remaining--;
        }

        // Copy the selected segment, counting only visible characters toward width.
        const char *copy_ptr = word_start;
        while (copy_ptr < text && written < max_out) {
            char current = *copy_ptr++;
            *out++ = current;
            written++;
            if (current != MD_FORMAT_TOGGLE) remaining--;
        }

        while (*text == ' ') text++;
    }
    *out = '\0';
    return (int)(text - original);
}

static void add_spacer(rendered_line_t *lines, uint8_t *heading_levels, int *count, int max_lines) {
    if (*count < max_lines) {
        lines[*count].text[0] = '\0';
        lines[*count].color = TEXT_COLOR_BLACK;
        lines[*count].attr = TEXT_ATTR_NORMAL;
        if (heading_levels) {
            heading_levels[*count] = 0;
        }
        (*count)++;
    }
}

static int read_next_block(FILE *f, markdown_block_t *block) {
    char line[512];
    block->text[0] = '\0';
    block->type = 0;
    block->heading_level = 0;
    block->start_pos = ftell(f);

    while (fgets(line, sizeof(line), f)) {
        strip_newline(line);

        if (is_blank_line(line)) {
            if (block->text[0]) break;
            continue;
        }

        if (is_tag_only_line(line)) {
            if (block->text[0]) break;
            continue;
        }

        if (is_heading_line(line)) {
            if (block->text[0]) {
                fseek(f, -((long)strlen(line) + 1), SEEK_CUR);
                break;
            }
            const char *cursor = line;
            while (*cursor == '#') {
                block->heading_level++;
                cursor++;
            }
            while (*cursor == ' ') cursor++;
            strncpy(block->text, cursor, sizeof(block->text) - 1);
            block->text[sizeof(block->text) - 1] = '\0';
            block->type = 1;
            break;
        }

        if (is_hr_line(line)) {
            if (block->text[0]) {
                fseek(f, -((long)strlen(line) + 1), SEEK_CUR);
                break;
            }
            block->text[0] = '-';
            block->text[1] = '\0';
            block->type = 3;
            break;
        }

        if (block->type == 0) block->type = 2;
        size_t remaining = sizeof(block->text) - strlen(block->text) - 1;
        size_t needed = strlen(line) + (block->text[0] ? 2 : 1);
        if (needed > remaining) {
            break;
        }
        if (block->text[0]) strcat(block->text, " ");
        strcat(block->text, line);
    }

    return block->text[0] != '\0';
}

static const char *append_wrapped_lines(rendered_line_t *lines, uint8_t *heading_levels, int heading_level, int *count, int max_lines, const char *src, int width, uint8_t color, uint8_t attr, char *remainder, size_t remainder_size) {
    while (*count < max_lines) {
        rendered_line_t line;
        int consumed = wrap_line(src, width, line.text, MAX_LINE_TEXT);
        if (consumed <= 0) {
            break;
        }

        line.color = color;
        line.attr = attr;
        lines[*count] = line;
        if (heading_levels) {
            heading_levels[*count] = (uint8_t)heading_level;
        }
        (*count)++;
        src += consumed;
    }

    if (*src) {
        strncpy(remainder, src, remainder_size - 1);
        remainder[remainder_size - 1] = '\0';
        return src;
    }

    remainder[0] = '\0';
    return NULL;
}

static void render_block(rendered_line_t *lines, uint8_t *heading_levels, int *count, int max_lines, FILE *f, const markdown_block_t *block, int screen_width) {
    const char *src = block->text;

    if (block->type == 3) {
        if (*count + 1 >= max_lines) {
            fseek(f, block->start_pos, SEEK_SET);
            carry_spacer = 0;
            return;
        }

        int w = screen_width;
        if (w > MAX_LINE_TEXT - 1) w = MAX_LINE_TEXT - 1;
        rendered_line_t line;
        memset(line.text, '-', w);
        line.text[w] = '\0';
        line.color = TEXT_COLOR_CYAN;
        line.attr = TEXT_ATTR_NORMAL;
        lines[(*count)++] = line;
        if (heading_levels) {
            heading_levels[*count - 1] = 0;
        }
        carry_spacer = 1;
        return;
    }

    uint8_t line_color = TEXT_COLOR_WHITE;
    uint8_t line_attr = TEXT_ATTR_NORMAL;
    if (block->type == 1) {
        line_color = (block->heading_level == 1) ? TEXT_COLOR_BRIGHT_WHITE : TEXT_COLOR_BRIGHT_CYAN;
        line_attr = TEXT_ATTR_BOLD;
    }

    // Track word boundaries for file position rewinding
    const char *word_start = block->text;
    long word_start_file_pos = block->start_pos;

    // Process text word by word
    const char *text_ptr = block->text;
    while (*src && *count < max_lines) {
        // Find the next word boundary
        const char *word_end = src;
        while (*word_end && *word_end != ' ' && *word_end != '\n' && *word_end != '\r') {
            word_end++;
        }

        // Calculate word length
        int word_len = word_end - src;
        if (word_len == 0) break;

        // Try to add this word to the current line
        // For simplicity, we'll use the existing wrap_line but track positions
        rendered_line_t line;
        int consumed = wrap_line(src, screen_width, line.text, MAX_LINE_TEXT);

        if (consumed <= 0) {
            break;
        }

        // Check if this would be the last line that fits
        if (*count + 1 >= max_lines) {
            // This is the last line - check if there's more text after it
            const char *remaining = src + consumed;
            while (*remaining && (*remaining == ' ' || *remaining == '\n' || *remaining == '\r')) {
                remaining++;
            }

            if (*remaining) {
                // There's more text that won't fit - rewind file to current word start
                fseek(f, word_start_file_pos, SEEK_SET);

                // Set state to indicate we're mid-element
                has_remainder = 1;
                if (block->type == 1) {
                    // Heading - set appropriate state
                    switch (block->heading_level) {
                        case 1: current_parser_state = MD_STATE_HEADING_1; break;
                        case 2: current_parser_state = MD_STATE_HEADING_2; break;
                        case 3: current_parser_state = MD_STATE_HEADING_3; break;
                        case 4: current_parser_state = MD_STATE_HEADING_4; break;
                        case 5: current_parser_state = MD_STATE_HEADING_5; break;
                        case 6: current_parser_state = MD_STATE_HEADING_6; break;
                        default: current_parser_state = MD_STATE_HEADING_1; break;
                    }
                    remainder_para_type = 1;
                    remainder_heading_level = block->heading_level;
                } else {
                    // Paragraph (default processing)
                    current_parser_state = MD_STATE_DEFAULT;
                    remainder_para_type = 0;
                    remainder_heading_level = 0;
                }
                carry_spacer = 0;
                return;
            }
        }

        // Word fits - add it to the page
        line.color = line_color;
        line.attr = line_attr;
        lines[*count] = line;
        if (heading_levels) {
            heading_levels[*count] = (uint8_t)((block->type == 1) ? block->heading_level : 0);
        }
        (*count)++;

        // Update word start position for the next iteration
        word_start_file_pos += (text_ptr - block->text) + consumed;
        text_ptr += consumed;
    }
}

int md_scan_page_with_levels(FILE *f, rendered_line_t *lines, uint8_t *heading_levels, int max_lines, int screen_width) {
    int count = 0;

    // Inter-page spacer if previous page ended with a complete block
    if (carry_spacer && count < max_lines) {
        if (count + 1 < max_lines) {
            add_spacer(lines, heading_levels, &count, max_lines);
            carry_spacer = 0;
        } else {
            carry_spacer = 1;
            return count;
        }
    }

    // If has_remainder is set, we're continuing from mid-paragraph
    // The file position is already at the correct place to continue reading
    // Just proceed to read new content from the file

    // Read new content from file
    while (count < max_lines) {
        markdown_block_t block;

        if (!read_next_block(f, &block)) {
            break;
        }

        normalize_markdown_text(block.text);
        if (!block.text[0]) {
            carry_spacer = 0;
            continue;
        }

        // Spacer before this block if previous block ended
        if (carry_spacer) {
            if (count + 1 >= max_lines) {
                // Not enough room for spacer + at least 1 line of content
                carry_spacer = 1;
                break;
            }
            add_spacer(lines, heading_levels, &count, max_lines);
            carry_spacer = 0;
        }

        render_block(lines, heading_levels, &count, max_lines, f, &block, screen_width);
    }

    return count;
}

int md_scan_page(FILE *f, rendered_line_t *lines, int max_lines, int screen_width) {
    return md_scan_page_with_levels(f, lines, NULL, max_lines, screen_width);
}

void md_clear_remainder(void) {
    has_remainder = 0;
    remainder_para_type = 0;
    remainder_heading_level = 0;
    carry_spacer = 0;
    in_tag = 0;
    current_parser_state = MD_STATE_DEFAULT;
}

void md_get_parser_state(md_parser_state_t *state) {
    if (!state) return;

    // Map current state to enum
    if (has_remainder && remainder_para_type == 1) {
        // Heading - map to appropriate heading level state
        switch (remainder_heading_level) {
            case 1: state->state = MD_STATE_HEADING_1; break;
            case 2: state->state = MD_STATE_HEADING_2; break;
            case 3: state->state = MD_STATE_HEADING_3; break;
            case 4: state->state = MD_STATE_HEADING_4; break;
            case 5: state->state = MD_STATE_HEADING_5; break;
            case 6: state->state = MD_STATE_HEADING_6; break;
            default: state->state = MD_STATE_HEADING_1; break;
        }
    } else {
        // Paragraph (default) or no remainder
        state->state = MD_STATE_DEFAULT;
    }

    state->carry_spacer = carry_spacer;
    state->in_tag = in_tag;
}

void md_set_parser_state(const md_parser_state_t *state) {
    if (!state) return;

    // Map enum back to internal variables
    current_parser_state = state->state;
    carry_spacer = state->carry_spacer;
    in_tag = state->in_tag;

    switch (state->state) {
        case MD_STATE_DEFAULT:
            // Default paragraph processing
            has_remainder = 0;
            remainder_para_type = 0;
            remainder_heading_level = 0;
            break;
        case MD_STATE_HEADING_1:
            has_remainder = 1;
            remainder_para_type = 1;
            remainder_heading_level = 1;
            break;
        case MD_STATE_HEADING_2:
            has_remainder = 1;
            remainder_para_type = 1;
            remainder_heading_level = 2;
            break;
        case MD_STATE_HEADING_3:
            has_remainder = 1;
            remainder_para_type = 1;
            remainder_heading_level = 3;
            break;
        case MD_STATE_HEADING_4:
            has_remainder = 1;
            remainder_para_type = 1;
            remainder_heading_level = 4;
            break;
        case MD_STATE_HEADING_5:
            has_remainder = 1;
            remainder_para_type = 1;
            remainder_heading_level = 5;
            break;
        case MD_STATE_HEADING_6:
            has_remainder = 1;
            remainder_para_type = 1;
            remainder_heading_level = 6;
            break;
    }
}

// Get the file offset where the current remainder starts
// This is the position that should be cached for the next page
long md_get_remainder_start_offset(void) {
    // Return the current file position MINUS the length of the remainder
    // This points to where the remainder text starts in the file
    if (has_remainder && para_remainder[0]) {
        long remainder_len = (long)strlen(para_remainder);
        long current_pos = -1;  // We need to track this externally
        // The caller needs to track the file position before md_scan_page advances it
        // For now, return 0 to indicate "calculate from current position - remainder length"
        return 0;
    }
    return 0;
}
