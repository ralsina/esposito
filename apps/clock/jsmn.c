#include "jsmn.h"

#include <stddef.h>

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, jsmntok_t *tokens, size_t num_tokens) {
    if (parser->toknext >= num_tokens) {
        return NULL;
    }
    jsmntok_t *token = &tokens[parser->toknext++];
    token->start = -1;
    token->end = -1;
    token->size = 0;
    token->parent = -1;
    token->type = JSMN_UNDEFINED;
    return token;
}

static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type, int start, int end) {
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens) {
    int start = (int)parser->pos;

    while (parser->pos < len) {
        char c = js[parser->pos];
        if (c == '\t' || c == '\r' || c == '\n' || c == ' ' || c == ',' || c == ']' || c == '}') {
            break;
        }
        if (c < 32 || c >= 127) {
            parser->pos = (unsigned int)start;
            return JSMN_ERROR_INVAL;
        }
        parser->pos++;
    }

    if (tokens == NULL) {
        parser->pos--;
        return 0;
    }

    jsmntok_t *token = jsmn_alloc_token(parser, tokens, num_tokens);
    if (token == NULL) {
        parser->pos = (unsigned int)start;
        return JSMN_ERROR_NOMEM;
    }
    jsmn_fill_token(token, JSMN_PRIMITIVE, start, (int)parser->pos);
    token->parent = parser->toksuper;
    parser->pos--;
    return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens) {
    int start = (int)parser->pos;

    parser->pos++;
    while (parser->pos < len) {
        char c = js[parser->pos];

        if (c == '"') {
            if (tokens == NULL) {
                return 0;
            }
            jsmntok_t *token = jsmn_alloc_token(parser, tokens, num_tokens);
            if (token == NULL) {
                parser->pos = (unsigned int)start;
                return JSMN_ERROR_NOMEM;
            }
            jsmn_fill_token(token, JSMN_STRING, start + 1, (int)parser->pos);
            token->parent = parser->toksuper;
            return 0;
        }

        if (c == '\\' && parser->pos + 1 < len) {
            char esc = js[parser->pos + 1];
            if (esc == '"' || esc == '/' || esc == '\\' || esc == 'b' || esc == 'f' || esc == 'r' || esc == 'n' || esc == 't') {
                parser->pos++;
                continue;
            }
            if (esc == 'u') {
                parser->pos++;
                for (int i = 0; i < 4; i++) {
                    if (parser->pos + 1 >= len) {
                        parser->pos = (unsigned int)start;
                        return JSMN_ERROR_PART;
                    }
                    char h = js[parser->pos + 1];
                    if (!((h >= '0' && h <= '9') || (h >= 'A' && h <= 'F') || (h >= 'a' && h <= 'f'))) {
                        parser->pos = (unsigned int)start;
                        return JSMN_ERROR_INVAL;
                    }
                    parser->pos++;
                }
                continue;
            }
            parser->pos = (unsigned int)start;
            return JSMN_ERROR_INVAL;
        }

        parser->pos++;
    }

    parser->pos = (unsigned int)start;
    return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser *parser) {
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser, const char *js, unsigned int len, jsmntok_t *tokens, unsigned int num_tokens) {
    int count = (int)parser->toknext;

    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];
        jsmntok_t *token;

        switch (c) {
            case '{':
            case '[':
                count++;
                token = jsmn_alloc_token(parser, tokens, num_tokens);
                if (token == NULL) {
                    return JSMN_ERROR_NOMEM;
                }
                if (parser->toksuper != -1) {
                    tokens[parser->toksuper].size++;
                    token->parent = parser->toksuper;
                }
                token->type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
                token->start = (int)parser->pos;
                parser->toksuper = (int)parser->toknext - 1;
                break;

            case '}':
            case ']': {
                jsmntype_t type = (c == '}') ? JSMN_OBJECT : JSMN_ARRAY;
                for (int index = (int)parser->toknext - 1; index >= 0; index--) {
                    token = &tokens[index];
                    if (token->start != -1 && token->end == -1) {
                        if (token->type != type) {
                            return JSMN_ERROR_INVAL;
                        }
                        token->end = (int)parser->pos + 1;
                        parser->toksuper = token->parent;
                        break;
                    }
                }
                break;
            }

            case '"':
                count++;
                if (jsmn_parse_string(parser, js, len, tokens, num_tokens) < 0) {
                    return JSMN_ERROR_INVAL;
                }
                if (parser->toksuper != -1) {
                    tokens[parser->toksuper].size++;
                }
                break;

            case '\t':
            case '\r':
            case '\n':
            case ' ':
            case ':':
            case ',':
                break;

            default:
                count++;
                if (jsmn_parse_primitive(parser, js, len, tokens, num_tokens) < 0) {
                    return JSMN_ERROR_INVAL;
                }
                if (parser->toksuper != -1) {
                    tokens[parser->toksuper].size++;
                }
                break;
        }
    }

    for (unsigned int index = parser->toknext; index > 0; index--) {
        if (tokens[index - 1].start != -1 && tokens[index - 1].end == -1) {
            return JSMN_ERROR_PART;
        }
    }

    return count;
}
