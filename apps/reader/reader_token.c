#include "reader_token.h"
#include <string.h>

// Read a raw byte from file/pushback
static int read_raw_byte(tokenizer_t *tokenizer) {
    if (tokenizer->pushback != -1) {
        int ch = tokenizer->pushback;
        tokenizer->pushback = -1;
        return ch;
    }
    while (true) {
        unsigned char c;
        if (fread(&c, 1, 1, tokenizer->file) == 0) return -1;
        tokenizer->current_pos++;
        if (c == '<') {
            while (true) {
                if (fread(&c, 1, 1, tokenizer->file) == 0) return -1;
                tokenizer->current_pos++;
                if (c == '>') break;
            }
            continue;
        }
        return (int)c;
    }
}

// Decode a UTF-8 codepoint starting with lead byte, consumes continuation bytes
static long decode_utf8(tokenizer_t *tokenizer, int lead) {
    int extra;
    long cp;
    if ((lead & 0xE0) == 0xC0) { extra = 1; cp = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { extra = 2; cp = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { extra = 3; cp = lead & 0x07; }
    else return lead;

    for (int i = 0; i < extra; i++) {
        int cb = read_raw_byte(tokenizer);
        if (cb < 0) return -1;
        if ((cb & 0xC0) != 0x80) {
            tokenizer->pushback = cb;
            return cp;
        }
        cp = (cp << 6) | (cb & 0x3F);
    }
    return cp;
}

// Append ASCII replacement chars to current word token
static void append_replacement(tokenizer_t *tokenizer, const char *s, int len) {
    int cap = (int)sizeof(tokenizer->current_token.text) - 1 - (int)tokenizer->current_token.text_len;
    if (len > cap) len = cap;
    memcpy(tokenizer->current_token.text + tokenizer->current_token.text_len, s, len);
    tokenizer->current_token.text_len += len;
    tokenizer->current_token.text[tokenizer->current_token.text_len] = '\0';
}

// Map typographic codepoint to ASCII replacement (returns 0 to skip, >0 replacement count)
int get_ascii_replacement(long cp, char *out) {
    switch (cp) {
        case 0x2013: case 0x2014: case 0x2212:
        case 0x2010: case 0x2011: case 0x2012:
            out[0] = '-'; return 1;
        case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032:
            out[0] = '\''; return 1;
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: case 0x00AB: case 0x00BB:
            out[0] = '"'; return 1;
        case 0x2026:
            out[0] = '.'; out[1] = '.'; out[2] = '.'; return 3;
        case 0x00A0:
            out[0] = ' '; return 1;
        case 0x2022: case 0x00B7:
            out[0] = '*'; return 1;
        default:
            return 0; // unknown, skip
    }
}

void tokenizer_init(tokenizer_t *tokenizer, FILE *file) {
    tokenizer->file = file;
    tokenizer->current_pos = 0;
    tokenizer->has_token = false;
    tokenizer->current_token.type = TOKEN_EOF;
    tokenizer->current_token.text[0] = '\0';
    tokenizer->current_token.text_len = 0;
    tokenizer->current_token.file_pos = 0;
    tokenizer->pushback = -1;
    tokenizer->next_page_start = 0;
}

bool tokenizer_next(tokenizer_t *tokenizer) {
    if (!tokenizer || !tokenizer->file) {
        return false;
    }

    int ch = read_raw_byte(tokenizer);
    if (ch < 0) {
        tokenizer->current_token.type = TOKEN_EOF;
        tokenizer->current_token.text[0] = '\0';
        tokenizer->current_token.text_len = 0;
        tokenizer->current_token.file_pos = tokenizer->current_pos;
        tokenizer->has_token = true;
        return false;
    }

    tokenizer->current_token.file_pos = tokenizer->current_pos - 1;

    // Handle non-ASCII (UTF-8) as a single-character word with asciification
    if (ch >= 0x80) {
        long cp = decode_utf8(tokenizer, ch);
        if (cp < 0) {
            // Partial sequence at EOF
            tokenizer->current_token.type = TOKEN_WORD;
            tokenizer->current_token.text[0] = (char)ch;
            tokenizer->current_token.text[1] = '\0';
            tokenizer->current_token.text_len = 1;
            tokenizer->has_token = true;
            return true;
        }
        char repl[4];
        int n = get_ascii_replacement(cp, repl);
        if (n == 0) {
            // Unknown codepoint, skip it and recurse
            return tokenizer_next(tokenizer);
        }
        if (n == 1 && repl[0] == ' ') {
            ch = ' '; // fall through to TOKEN_SPACE handling
        } else {
            tokenizer->current_token.type = TOKEN_WORD;
            tokenizer->current_token.text_len = 0;
            append_replacement(tokenizer, repl, n);
            // Continue reading the rest of the word
            while (tokenizer->current_token.text_len < sizeof(tokenizer->current_token.text) - 1) {
                int next_ch = read_raw_byte(tokenizer);
                if (next_ch < 0) {
                    break;
                }
                if (next_ch == ' ' || next_ch == '\t' || next_ch == '\n' || next_ch == '#') {
                    tokenizer->pushback = next_ch;
                    break;
                }
                if (next_ch == '\\') {
                    int escaped = read_raw_byte(tokenizer);
                    if (escaped < 0) break;
                    if (escaped == '\\') {
                        append_replacement(tokenizer, "\\", 1);
                    } else {
                        if (escaped == ' ' || escaped == '\t' || escaped == '\n' || escaped == '#') {
                            tokenizer->pushback = escaped;
                            break;
                        }
                        if (escaped >= 0x80) {
                            long cp = decode_utf8(tokenizer, escaped);
                            if (cp < 0) break;
                            char repl[4];
                            int n = get_ascii_replacement(cp, repl);
                            if (n > 0) append_replacement(tokenizer, repl, n);
                        } else {
                            tokenizer->current_token.text[tokenizer->current_token.text_len] = (char)escaped;
                            tokenizer->current_token.text_len++;
                        }
                    }
                    continue;
                }
                if (next_ch >= 0x80) {
                    long next_cp = decode_utf8(tokenizer, next_ch);
                    if (next_cp < 0) break;
                    char next_repl[4];
                    int next_n = get_ascii_replacement(next_cp, next_repl);
                    if (next_n > 0) {
                        append_replacement(tokenizer, next_repl, next_n);
                    }
                    continue;
                }
                tokenizer->current_token.text[tokenizer->current_token.text_len] = (char)next_ch;
                tokenizer->current_token.text_len++;
                tokenizer->current_pos++;
            }
            tokenizer->current_token.text[tokenizer->current_token.text_len] = '\0';
            tokenizer->has_token = true;
            return true;
        }
    }

    // Handle backslash: ignore unless it's escaped
    if (ch == '\\') {
        int next = read_raw_byte(tokenizer);
        if (next < 0) {
            return tokenizer_next(tokenizer);
        }
        if (next == '\\') {
            ch = '\\';
        } else {
            tokenizer->pushback = next;
            return tokenizer_next(tokenizer);
        }
    }

    // Classify the character
    if (ch == '#') {
        tokenizer->current_token.type = TOKEN_HASH;
        tokenizer->current_token.text[0] = '#';
        tokenizer->current_token.text[1] = '\0';
        tokenizer->current_token.text_len = 1;
        tokenizer->has_token = true;
        return true;
    }

    if (ch == '>') {
        tokenizer->current_token.type = TOKEN_GT;
        tokenizer->current_token.text[0] = '>';
        tokenizer->current_token.text[1] = '\0';
        tokenizer->current_token.text_len = 1;
        tokenizer->has_token = true;
        return true;
    }

    if (ch == '\n') {
        tokenizer->current_token.type = TOKEN_NEWLINE;
        tokenizer->current_token.text[0] = '\n';
        tokenizer->current_token.text[1] = '\0';
        tokenizer->current_token.text_len = 1;
        tokenizer->has_token = true;
        return true;
    }

    if (ch == ' ' || ch == '\t') {
        tokenizer->current_token.type = TOKEN_SPACE;
        tokenizer->current_token.text[0] = ch;
        tokenizer->current_token.text[1] = '\0';
        tokenizer->current_token.text_len = 1;
        tokenizer->has_token = true;
        return true;
    }

    // It's a word character - read the entire word
    tokenizer->current_token.type = TOKEN_WORD;
    tokenizer->current_token.text[0] = (char)ch;
    tokenizer->current_token.text_len = 1;

    // Keep reading word characters
    while (tokenizer->current_token.text_len < sizeof(tokenizer->current_token.text) - 1) {
        int next_ch = read_raw_byte(tokenizer);
        if (next_ch < 0) {
            break;
        }

        if (next_ch == ' ' || next_ch == '\t' || next_ch == '\n' || next_ch == '#' || next_ch == '<') {
            tokenizer->pushback = next_ch;
            break;
        }

        if (next_ch == '\\') {
            int escaped = read_raw_byte(tokenizer);
            if (escaped < 0) break;
            if (escaped == '\\') {
                tokenizer->current_token.text[tokenizer->current_token.text_len] = '\\';
                tokenizer->current_token.text_len++;
            } else {
                if (escaped == ' ' || escaped == '\t' || escaped == '\n' || escaped == '#' || escaped == '<') {
                    tokenizer->pushback = escaped;
                    break;
                }
                if (escaped >= 0x80) {
                    long cp = decode_utf8(tokenizer, escaped);
                    if (cp < 0) break;
                    char repl[4];
                    int n = get_ascii_replacement(cp, repl);
                    if (n > 0) append_replacement(tokenizer, repl, n);
                } else {
                    tokenizer->current_token.text[tokenizer->current_token.text_len] = (char)escaped;
                    tokenizer->current_token.text_len++;
                }
            }
            continue;
        }

        if (next_ch >= 0x80) {
            long cp = decode_utf8(tokenizer, next_ch);
            if (cp < 0) break;
            char repl[4];
            int n = get_ascii_replacement(cp, repl);
            if (n > 0) {
                append_replacement(tokenizer, repl, n);
            }
            continue;
        }

        tokenizer->current_token.text[tokenizer->current_token.text_len] = (char)next_ch;
        tokenizer->current_token.text_len++;
    }

    tokenizer->current_token.text[tokenizer->current_token.text_len] = '\0';
    tokenizer->has_token = true;
    return true;
}

token_t* tokenizer_current(tokenizer_t *tokenizer) {
    if (!tokenizer || !tokenizer->has_token) {
        return NULL;
    }
    return &tokenizer->current_token;
}

long tokenizer_get_position(tokenizer_t *tokenizer) {
    if (!tokenizer) {
        return 0;
    }
    return tokenizer->current_pos;
}

void tokenizer_set_position(tokenizer_t *tokenizer, long position) {
    if (!tokenizer || !tokenizer->file) {
        return;
    }

    fseek(tokenizer->file, position, SEEK_SET);
    tokenizer->current_pos = position;
    tokenizer->has_token = false;
    tokenizer->pushback = -1;
}