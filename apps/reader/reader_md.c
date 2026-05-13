#include "reader_md.h"
#include "text_mode.h"
#include <string.h>
#include <stdio.h>

#define MD_LINE_BUF 512
#define PARA_BUF_SIZE 4096

static char md_para[PARA_BUF_SIZE];
static char md_line[MD_LINE_BUF];
static char para_remainder[PARA_BUF_SIZE];
static int has_remainder = 0;
static int remainder_para_type = 0;
static int remainder_heading_level = 0;
static int carry_spacer = 0;
static int in_tag = 0;

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

                        *dst++ = MD_LINK_TOGGLE;
                        while (text < text_end && !is_space((unsigned char)*text)) {
                            *dst++ = *text++;
                        }
                        *dst++ = MD_LINK_TOGGLE;
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

static void normalize_markdown_text(char *line) {
    strip_html(line);
    asciify(line);
    strip_markdown_images(line);
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
            if (*probe != MD_LINK_TOGGLE) word_chars++;
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
                if (*split != MD_LINK_TOGGLE) visible++;
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
            if (current != MD_LINK_TOGGLE) remaining--;
        }

        while (*text == ' ') text++;
    }
    *out = '\0';
    return (int)(text - original);
}

static void add_spacer(rendered_line_t *lines, int *count, int max_lines) {
    if (*count < max_lines) {
        lines[*count].text[0] = '\0';
        lines[*count].color = TEXT_COLOR_BLACK;
        lines[*count].attr = TEXT_ATTR_NORMAL;
        (*count)++;
    }
}

static const char *append_wrapped_lines(rendered_line_t *lines, int *count, int max_lines, const char *src, int width, uint8_t color, uint8_t attr, char *remainder, size_t remainder_size) {
    while (*count < max_lines) {
        rendered_line_t line;
        int consumed = wrap_line(src, width, line.text, MAX_LINE_TEXT);
        if (consumed <= 0) {
            break;
        }

        line.color = color;
        line.attr = attr;
        lines[*count] = line;
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

int md_scan_page(FILE *f, rendered_line_t *lines, int max_lines, int screen_width) {
    int count = 0;

    // Inter-page spacer if previous page ended with a complete block
    if (carry_spacer && count < max_lines) {
        if (count + 1 < max_lines) {
            add_spacer(lines, &count, max_lines);
            carry_spacer = 0;
        } else {
            carry_spacer = 1;
            return count;
        }
    }

    // Process any paragraph remainder from a previous mid-paragraph break
    if (has_remainder) {
        const char *src = para_remainder;
        uint8_t rem_color = TEXT_COLOR_WHITE;
        uint8_t rem_attr = TEXT_ATTR_NORMAL;

        if (remainder_para_type == 1) {
            rem_color = (remainder_heading_level == 1) ? TEXT_COLOR_BRIGHT_WHITE : TEXT_COLOR_BRIGHT_CYAN;
            rem_attr = TEXT_ATTR_BOLD;
        }

        if (append_wrapped_lines(lines, &count, max_lines, src, screen_width, rem_color, rem_attr, para_remainder, sizeof(para_remainder))) {
            carry_spacer = 0;
            return count;
        }

        has_remainder = 0;
        remainder_para_type = 0;
        remainder_heading_level = 0;
        carry_spacer = 1;
    }

    // Read new content from file
    while (count < max_lines) {
        long para_start_pos = ftell(f);
        md_para[0] = '\0';
        int para_type = 0;
        int heading_level = 0;

        // Accumulate one paragraph (joining lines between blank lines)
        while (fgets(md_line, sizeof(md_line), f)) {
            strip_newline(md_line);

            if (is_blank_line(md_line)) {
                if (md_para[0]) break;
                continue;
            }

            if (is_tag_only_line(md_line)) {
                if (md_para[0]) break;
                continue;
            }

            if (is_heading_line(md_line)) {
                if (md_para[0]) {
                    fseek(f, -((long)strlen(md_line) + 1), SEEK_CUR);
                    break;
                }
                const char *p = md_line;
                while (*p == '#') { heading_level++; p++; }
                while (*p == ' ') p++;
                strcpy(md_para, p);
                para_type = 1;
                break;
            }

            if (is_hr_line(md_line)) {
                if (md_para[0]) {
                    fseek(f, -((long)strlen(md_line) + 1), SEEK_CUR);
                    break;
                }
                md_para[0] = '-';
                md_para[1] = '\0';
                para_type = 3;
                break;
            }

            if (para_type == 0) para_type = 2;
            size_t remaining_buf = sizeof(md_para) - strlen(md_para) - 1;
            size_t needed = strlen(md_line) + (md_para[0] ? 2 : 1);
            if (needed > remaining_buf) {
                break;
            }
            if (md_para[0]) strcat(md_para, " ");
            strcat(md_para, md_line);
        }

        if (!md_para[0]) break;

        normalize_markdown_text(md_para);
        if (!md_para[0]) {
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
            add_spacer(lines, &count, max_lines);
            carry_spacer = 0;
        }

        // Word-wrap the paragraph one line at a time
        const char *src = md_para;

        if (para_type == 3) {
            if (count + 1 >= max_lines) {
                fseek(f, para_start_pos, SEEK_SET);
                carry_spacer = 0;
                break;
            }
            int w = screen_width;
            if (w > MAX_LINE_TEXT - 1) w = MAX_LINE_TEXT - 1;
            rendered_line_t line;
            memset(line.text, '-', w);
            line.text[w] = '\0';
            line.color = TEXT_COLOR_CYAN;
            line.attr = TEXT_ATTR_NORMAL;
            lines[count++] = line;
            carry_spacer = 1;
        } else {
            uint8_t line_color = TEXT_COLOR_WHITE;
            uint8_t line_attr = TEXT_ATTR_NORMAL;
            if (para_type == 1) {
                line_color = (heading_level == 1) ? TEXT_COLOR_BRIGHT_WHITE : TEXT_COLOR_BRIGHT_CYAN;
                line_attr = TEXT_ATTR_BOLD;
            }

            if (append_wrapped_lines(lines, &count, max_lines, src, screen_width, line_color, line_attr, para_remainder, sizeof(para_remainder))) {
                has_remainder = 1;
                remainder_para_type = para_type;
                remainder_heading_level = heading_level;
                carry_spacer = 0;
            } else {
                carry_spacer = 1;
            }
        }
    }

    return count;
}

void md_clear_remainder(void) {
    has_remainder = 0;
    remainder_para_type = 0;
    remainder_heading_level = 0;
    carry_spacer = 0;
    in_tag = 0;
}
