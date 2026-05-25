#ifndef READER_TOKEN_H
#define READER_TOKEN_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Token types for the markdown tokenizer
typedef enum {
    TOKEN_WORD,              // Regular text content
    TOKEN_HASH,              // # character (heading marker)
    TOKEN_NEWLINE,           // \n character
    TOKEN_SPACE,             // Space or tab
    TOKEN_EOF,               // End of file
} token_type_t;

// Token structure
typedef struct {
    token_type_t type;
    char text[128];          // Text content (for WORD tokens)
    size_t text_len;         // Length of text
    long file_pos;           // File position where token starts
} token_t;

// Tokenizer state
typedef struct {
    FILE *file;              // File being tokenized
    long current_pos;        // Current file position
    token_t current_token;    // Most recently read token
    bool has_token;          // Whether current_token is valid
} tokenizer_t;

// Initialize tokenizer
void tokenizer_init(tokenizer_t *tokenizer, FILE *file);

// Get next token from file
bool tokenizer_next(tokenizer_t *tokenizer);

// Get current token (re-gets the last token)
token_t* tokenizer_current(tokenizer_t *tokenizer);

// Get current file position
long tokenizer_get_position(tokenizer_t *tokenizer);

// Set file position (for jumping to cached position)
void tokenizer_set_position(tokenizer_t *tokenizer, long position);

#endif