#include "core_json.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#ifndef coreJSON_ASSERT
#define coreJSON_ASSERT( expr ) ((void)0)
#endif

#ifndef JSON_MAX_DEPTH
#define JSON_MAX_DEPTH 32
#endif

static bool is_digit_char( char ch ) {
    return ( ch >= '0' ) && ( ch <= '9' );
}

static bool is_hex_char( char ch ) {
    return ( ( ch >= '0' ) && ( ch <= '9' ) ) ||
           ( ( ch >= 'a' ) && ( ch <= 'f' ) ) ||
           ( ( ch >= 'A' ) && ( ch <= 'F' ) );
}

static void skip_spaces( const char * buf, size_t max, size_t * index ) {
    while( buf != NULL && index != NULL && *index < max &&
           ( buf[ *index ] == ' ' || buf[ *index ] == '\t' || buf[ *index ] == '\n' || buf[ *index ] == '\r' ) ) {
        ( *index )++;
    }
}

static bool match_literal( const char * buf, size_t max, size_t * index, const char * literal ) {
    size_t length = strlen( literal );
    if( *index + length > max ) {
        return false;
    }
    if( strncmp( &buf[ *index ], literal, length ) != 0 ) {
        return false;
    }
    *index += length;
    return true;
}

static bool parse_string( const char * buf, size_t max, size_t * index ) {
    if( *index >= max || buf[ *index ] != '"' ) {
        return false;
    }
    ( *index )++;
    while( *index < max ) {
        char ch = buf[ *index ];
        if( ch == '"' ) {
            ( *index )++;
            return true;
        }
        if( ch == '\\' ) {
            ( *index )++;
            if( *index >= max ) {
                return false;
            }
            ch = buf[ *index ];
            if( ch == 'u' ) {
                for( int i = 0; i < 4; i++ ) {
                    ( *index )++;
                    if( *index >= max || !is_hex_char( buf[ *index ] ) ) {
                        return false;
                    }
                }
            }
        }
        else if( ( unsigned char ) ch < 0x20U ) {
            return false;
        }
        ( *index )++;
    }
    return false;
}

static bool parse_number( const char * buf, size_t max, size_t * index ) {
    size_t i = *index;
    if( i < max && buf[ i ] == '-' ) {
        i++;
    }
    if( i >= max || !is_digit_char( buf[ i ] ) ) {
        return false;
    }
    if( buf[ i ] == '0' ) {
        i++;
    }
    else {
        while( i < max && is_digit_char( buf[ i ] ) ) {
            i++;
        }
    }
    if( i < max && buf[ i ] == '.' ) {
        i++;
        if( i >= max || !is_digit_char( buf[ i ] ) ) {
            return false;
        }
        while( i < max && is_digit_char( buf[ i ] ) ) {
            i++;
        }
    }
    if( i < max && ( buf[ i ] == 'e' || buf[ i ] == 'E' ) ) {
        i++;
        if( i < max && ( buf[ i ] == '+' || buf[ i ] == '-' ) ) {
            i++;
        }
        if( i >= max || !is_digit_char( buf[ i ] ) ) {
            return false;
        }
        while( i < max && is_digit_char( buf[ i ] ) ) {
            i++;
        }
    }
    *index = i;
    return true;
}

static bool parse_value( const char * buf, size_t max, size_t * index, int depth );

static bool parse_array( const char * buf, size_t max, size_t * index, int depth ) {
    if( depth >= JSON_MAX_DEPTH ) {
        return false;
    }
    if( *index >= max || buf[ *index ] != '[' ) {
        return false;
    }
    ( *index )++;
    skip_spaces( buf, max, index );
    if( *index < max && buf[ *index ] == ']' ) {
        ( *index )++;
        return true;
    }
    while( *index < max ) {
        if( !parse_value( buf, max, index, depth + 1 ) ) {
            return false;
        }
        skip_spaces( buf, max, index );
        if( *index < max && buf[ *index ] == ',' ) {
            ( *index )++;
            skip_spaces( buf, max, index );
            continue;
        }
        if( *index < max && buf[ *index ] == ']' ) {
            ( *index )++;
            return true;
        }
        return false;
    }
    return false;
}

static bool parse_object( const char * buf, size_t max, size_t * index, int depth ) {
    if( depth >= JSON_MAX_DEPTH ) {
        return false;
    }
    if( *index >= max || buf[ *index ] != '{' ) {
        return false;
    }
    ( *index )++;
    skip_spaces( buf, max, index );
    if( *index < max && buf[ *index ] == '}' ) {
        ( *index )++;
        return true;
    }
    while( *index < max ) {
        if( !parse_string( buf, max, index ) ) {
            return false;
        }
        skip_spaces( buf, max, index );
        if( *index >= max || buf[ *index ] != ':' ) {
            return false;
        }
        ( *index )++;
        skip_spaces( buf, max, index );
        if( !parse_value( buf, max, index, depth + 1 ) ) {
            return false;
        }
        skip_spaces( buf, max, index );
        if( *index < max && buf[ *index ] == ',' ) {
            ( *index )++;
            skip_spaces( buf, max, index );
            continue;
        }
        if( *index < max && buf[ *index ] == '}' ) {
            ( *index )++;
            return true;
        }
        return false;
    }
    return false;
}

static bool parse_value( const char * buf, size_t max, size_t * index, int depth ) {
    skip_spaces( buf, max, index );
    if( *index >= max ) {
        return false;
    }
    switch( buf[ *index ] ) {
        case '"':
            return parse_string( buf, max, index );
        case '{':
            return parse_object( buf, max, index, depth );
        case '[':
            return parse_array( buf, max, index, depth );
        case 't':
            return match_literal( buf, max, index, "true" );
        case 'f':
            return match_literal( buf, max, index, "false" );
        case 'n':
            return match_literal( buf, max, index, "null" );
        default:
            return parse_number( buf, max, index );
    }
}

static JSONTypes_t json_type_at( char ch ) {
    switch( ch ) {
        case '"': return JSONString;
        case '{': return JSONObject;
        case '[': return JSONArray;
        case 't': return JSONTrue;
        case 'f': return JSONFalse;
        case 'n': return JSONNull;
        default: return JSONNumber;
    }
}

static bool search_path( const char * buf,
                         size_t max,
                         size_t value_start,
                         size_t value_end,
                         const char * query,
                         size_t query_len,
                         size_t query_pos,
                         const char ** outValue,
                         size_t * outValueLength,
                         JSONTypes_t * outType,
                         int depth );

static bool search_object( const char * buf,
                          size_t max,
                          size_t value_start,
                          size_t value_end,
                          const char * query,
                          size_t query_len,
                          size_t query_pos,
                          const char ** outValue,
                          size_t * outValueLength,
                          JSONTypes_t * outType,
                          int depth ) {
    size_t index = value_start;
    if( index >= value_end || buf[ index ] != '{' ) {
        return false;
    }
    index++;
    skip_spaces( buf, max, &index );
    while( index < value_end && buf[ index ] != '}' ) {
        size_t key_start = index + 1;
        if( !parse_string( buf, max, &index ) ) {
            return false;
        }
        size_t key_end = index - 1;
        size_t key_len = key_end - key_start;
        skip_spaces( buf, max, &index );
        if( index >= value_end || buf[ index ] != ':' ) {
            return false;
        }
        index++;
        skip_spaces( buf, max, &index );
        size_t child_start = index;
        if( !parse_value( buf, max, &index, depth + 1 ) ) {
            return false;
        }
        size_t child_end = index;
        if( query_pos + key_len <= query_len &&
            strncmp( &query[ query_pos ], &buf[ key_start ], key_len ) == 0 ) {
            size_t next_pos = query_pos + key_len;
            if( next_pos == query_len ) {
                *outValue = &buf[ child_start ];
                *outValueLength = child_end - child_start;
                if( outType != NULL ) {
                    *outType = json_type_at( buf[ child_start ] );
                }
                return true;
            }
            if( query[ next_pos ] == '.' ) {
                return search_path( buf, max, child_start, child_end, query, query_len, next_pos + 1, outValue, outValueLength, outType, depth + 1 );
            }
        }
        skip_spaces( buf, max, &index );
        if( index < value_end && buf[ index ] == ',' ) {
            index++;
            skip_spaces( buf, max, &index );
            continue;
        }
        break;
    }
    return false;
}

static bool search_array( const char * buf,
                         size_t max,
                         size_t value_start,
                         size_t value_end,
                         const char * query,
                         size_t query_len,
                         size_t query_pos,
                         const char ** outValue,
                         size_t * outValueLength,
                         JSONTypes_t * outType,
                         int depth ) {
    size_t index = value_start;
    size_t element = 0;
    if( index >= value_end || buf[ index ] != '[' ) {
        return false;
    }
    index++;
    skip_spaces( buf, max, &index );
    if( query_pos >= query_len || query[ query_pos ] != '[' ) {
        return false;
    }
    query_pos++;
    int32_t wanted = 0;
    if( query_pos >= query_len || !is_digit_char( query[ query_pos ] ) ) {
        return false;
    }
    while( query_pos < query_len && is_digit_char( query[ query_pos ] ) ) {
        wanted = wanted * 10 + ( query[ query_pos ] - '0' );
        query_pos++;
    }
    if( query_pos >= query_len || query[ query_pos ] != ']' ) {
        return false;
    }
    query_pos++;
    if( query_pos == query_len ) {
        /* If the query ends on the index, return that element. */
    }
    while( index < value_end && buf[ index ] != ']' ) {
        size_t child_start = index;
        if( !parse_value( buf, max, &index, depth + 1 ) ) {
            return false;
        }
        if( element == ( size_t ) wanted ) {
            if( query_pos == query_len ) {
                *outValue = &buf[ child_start ];
                *outValueLength = index - child_start;
                if( outType != NULL ) {
                    *outType = json_type_at( buf[ child_start ] );
                }
                return true;
            }
            if( query[ query_pos ] == '.' ) {
                return search_path( buf, max, child_start, index, query, query_len, query_pos + 1, outValue, outValueLength, outType, depth + 1 );
            }
        }
        skip_spaces( buf, max, &index );
        if( index < value_end && buf[ index ] == ',' ) {
            index++;
            skip_spaces( buf, max, &index );
        }
        element++;
    }
    return false;
}

static bool search_path( const char * buf,
                         size_t max,
                         size_t value_start,
                         size_t value_end,
                         const char * query,
                         size_t query_len,
                         size_t query_pos,
                         const char ** outValue,
                         size_t * outValueLength,
                         JSONTypes_t * outType,
                         int depth ) {
    if( depth >= JSON_MAX_DEPTH ) {
        return false;
    }
    skip_spaces( buf, max, &value_start );
    if( query_pos >= query_len ) {
        *outValue = &buf[ value_start ];
        *outValueLength = value_end - value_start;
        if( outType != NULL ) {
            *outType = json_type_at( buf[ value_start ] );
        }
        return true;
    }

    if( buf[ value_start ] == '{' ) {
        return search_object( buf, max, value_start, value_end, query, query_len, query_pos, outValue, outValueLength, outType, depth );
    }
    if( buf[ value_start ] == '[' ) {
        return search_array( buf, max, value_start, value_end, query, query_len, query_pos, outValue, outValueLength, outType, depth );
    }

    return false;
}

JSONStatus_t JSON_Validate( const char * buf, size_t max ) {
    size_t index = 0;
    if( buf == NULL ) {
        return JSONNullParameter;
    }
    if( max == 0 ) {
        return JSONBadParameter;
    }
    skip_spaces( buf, max, &index );
    if( !parse_value( buf, max, &index, 0 ) ) {
        return JSONIllegalDocument;
    }
    skip_spaces( buf, max, &index );
    return ( index == max ) ? JSONSuccess : JSONIllegalDocument;
}

JSONStatus_t JSON_SearchConst( const char * buf,
                               size_t max,
                               const char * query,
                               size_t queryLength,
                               const char ** outValue,
                               size_t * outValueLength,
                               JSONTypes_t * outType ) {
    size_t index = 0;
    size_t value_end = max;
    if( buf == NULL || query == NULL || outValue == NULL || outValueLength == NULL ) {
        return JSONNullParameter;
    }
    if( max == 0 || queryLength == 0 ) {
        return JSONBadParameter;
    }
    skip_spaces( buf, max, &index );
    if( !parse_value( buf, max, &index, 0 ) ) {
        return JSONIllegalDocument;
    }
    value_end = index;
    if( !search_path( buf, max, 0, value_end, query, queryLength, 0, outValue, outValueLength, outType, 0 ) ) {
        return JSONNotFound;
    }
    return JSONSuccess;
}

JSONStatus_t JSON_SearchT( char * buf,
                           size_t max,
                           const char * query,
                           size_t queryLength,
                           char ** outValue,
                           size_t * outValueLength,
                           JSONTypes_t * outType ) {
    return JSON_SearchConst( ( const char * ) buf, max, query, queryLength, ( const char ** ) outValue, outValueLength, outType );
}

JSONStatus_t JSON_Iterate( const char * buf,
                           size_t max,
                           size_t * start,
                           size_t * next,
                           JSONPair_t * outPair ) {
    ( void ) buf;
    ( void ) max;
    ( void ) start;
    ( void ) next;
    ( void ) outPair;
    return JSONNotFound;
}