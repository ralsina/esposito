#include "reader_token.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>

void tokenizer_init(tokenizer_t *tokenizer, FILE *file) {
    tokenizer->file = file;
    tokenizer->current_pos = 0;
    tokenizer->has_token = false;
    tokenizer->current_token.type = TOKEN_EOF;
    tokenizer->current_token.text[0] = '\0';
    tokenizer->current_token.text_len = 0;
    tokenizer->current_token.file_pos = 0;
}

bool tokenizer_next(tokenizer_t *tokenizer) {
    if (!tokenizer || !tokenizer->file) {
        return false;
    }

    int ch = fgetc(tokenizer->file);
    if (ch == EOF) {
        tokenizer->current_token.type = TOKEN_EOF;
        tokenizer->current_token.text[0] = '\0';
        tokenizer->current_token.text_len = 0;
        tokenizer->current_token.file_pos = tokenizer->current_pos;
        tokenizer->has_token = true;
        return false;
    }

    tokenizer->current_pos++;
    tokenizer->current_token.file_pos = tokenizer->current_pos - 1;

    // Classify the character
    if (ch == '#') {
        tokenizer->current_token.type = TOKEN_HASH;
        tokenizer->current_token.text[0] = '#';
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
        int next_ch = fgetc(tokenizer->file);
        if (next_ch == EOF) {
            tokenizer->current_pos++;
            break;
        }

        // Check if this is still a word character
        if (next_ch == ' ' || next_ch == '\t' || next_ch == '\n' || next_ch == '#') {
            // Not a word character, put it back
            ungetc(next_ch, tokenizer->file);
            break;
        }

        tokenizer->current_token.text[tokenizer->current_token.text_len] = (char)next_ch;
        tokenizer->current_token.text_len++;
        tokenizer->current_pos++;
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
}