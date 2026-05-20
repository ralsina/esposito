/*
 * Minimal coreJSON-style API used by the clock app.
 * This is a small embedded parser that supports validation and key search.
 */
#ifndef CORE_JSON_H_
#define CORE_JSON_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSONPartial = 0,
    JSONSuccess,
    JSONIllegalDocument,
    JSONMaxDepthExceeded,
    JSONNotFound,
    JSONNullParameter,
    JSONBadParameter
} JSONStatus_t;

typedef enum {
    JSONInvalid = 0,
    JSONString,
    JSONNumber,
    JSONTrue,
    JSONFalse,
    JSONNull,
    JSONObject,
    JSONArray
} JSONTypes_t;

#define JSON_Search( buf, max, query, queryLength, outValue, outValueLength ) \
    JSON_SearchT( buf, max, query, queryLength, outValue, outValueLength, NULL )

typedef struct
{
    const char * key;
    size_t keyLength;
    const char * value;
    size_t valueLength;
    JSONTypes_t jsonType;
} JSONPair_t;

JSONStatus_t JSON_Validate( const char * buf, size_t max );
JSONStatus_t JSON_SearchConst( const char * buf,
                               size_t max,
                               const char * query,
                               size_t queryLength,
                               const char ** outValue,
                               size_t * outValueLength,
                               JSONTypes_t * outType );
JSONStatus_t JSON_SearchT( char * buf,
                           size_t max,
                           const char * query,
                           size_t queryLength,
                           char ** outValue,
                           size_t * outValueLength,
                           JSONTypes_t * outType );
JSONStatus_t JSON_Iterate( const char * buf,
                           size_t max,
                           size_t * start,
                           size_t * next,
                           JSONPair_t * outPair );

#ifdef __cplusplus
}
#endif

#endif