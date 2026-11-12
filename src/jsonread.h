#ifndef REMEMBER_JSONREAD_H
#define REMEMBER_JSONREAD_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Strict RFC 8259 JSON reader (plan 23 stage 1, D3, P11). Pure: no I/O.
 * One named nesting constant. No document size cap. Rejects trailing garbage,
 * NaN, Infinity, invalid UTF-8, and lone surrogates. Each value carries the
 * 1-based line where its first byte starts.
 */

enum { JSONREAD_NESTING_MAX = 64 };

typedef enum {
    JSONREAD_OK = 0,
    JSONREAD_ERR_SYNTAX,  /* bad token, trailing garbage, NaN/Infinity, … */
    JSONREAD_ERR_UTF8,    /* invalid UTF-8 or a lone surrogate escape */
    JSONREAD_ERR_NESTING, /* deeper than JSONREAD_NESTING_MAX */
    JSONREAD_ERR_OOM,
    JSONREAD_ERR_INTERNAL /* caller contract (NULL out, and similar) */
} JsonReadStatus;

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER, /* raw number text from the source (no leading +) */
    JSON_STRING, /* unescaped UTF-8 */
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char *key; /* unescaped heap string; free with the tree */
    size_t key_len;
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonType type;
    int line;      /* 1-based line of this value's first non-ws byte */
    size_t src_at; /* this value's bytes are src[src_at .. src_at + src_len) */
    size_t src_len;
    union {
        bool boolean;
        struct {
            char *bytes;
            size_t len;
        } number;
        struct {
            char *bytes;
            size_t len;
        } string;
        struct {
            JsonValue **items;
            size_t n;
        } array;
        struct {
            JsonMember *members;
            size_t n;
        } object;
    } u;
};

/*
 * Parse src[0..len) as one JSON value, then only optional whitespace.
 * On JSONREAD_OK, *out is a heap tree; free it with jsonread_free.
 * On failure *out is NULL.
 */
JsonReadStatus jsonread_parse(const char *src, size_t len, JsonValue **out);

void jsonread_free(JsonValue *v);

const char *jsonread_status_string(JsonReadStatus st);

#endif /* REMEMBER_JSONREAD_H */
