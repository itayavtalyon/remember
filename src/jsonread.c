#include "jsonread.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_alloc.h" /* IWYU pragma: keep — REMEMBER_TEST_HOOKS malloc seam */

/*
 * Strict RFC 8259 recursive-descent reader. Depth counts every nested
 * array/object value; JSONREAD_NESTING_MAX is inclusive of the outermost.
 */

typedef struct {
    const char *src;
    size_t len;
    size_t at;
    int line;
    int depth;
    JsonReadStatus err;
    char pad_[4]; /* -Wpadded: err is 4 bytes on LP64 */
} Parser;

static void set_err(Parser *p, JsonReadStatus st)
{
    if (p->err == JSONREAD_OK) {
        p->err = st;
    }
}

static bool at_end(const Parser *p)
{
    return p->at >= p->len;
}

static unsigned char peek(const Parser *p)
{
    if (at_end(p)) {
        return 0;
    }
    return (unsigned char)p->src[p->at];
}

static unsigned char next(Parser *p)
{
    unsigned char c = 0;

    if (at_end(p)) {
        return 0;
    }
    c = (unsigned char)p->src[p->at++];
    if (c == '\n') {
        p->line++;
    }
    return c;
}

static bool is_ws(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void skip_ws(Parser *p)
{
    while (!at_end(p) && is_ws(peek(p))) {
        (void)next(p);
    }
}

static JsonValue *alloc_value(Parser *p, JsonType type, int line)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));

    if (v == NULL) {
        set_err(p, JSONREAD_ERR_OOM);
        return NULL;
    }
    v->type = type;
    v->line = line;
    return v;
}

static void free_value(JsonValue *v);

static void free_members(JsonMember *members, size_t n)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        free(members[i].key);
        free_value(members[i].value);
    }
    free(members);
}

static void free_value(JsonValue *v)
{
    size_t i = 0;

    if (v == NULL) {
        return;
    }
    switch (v->type) {
    case JSON_NULL:
    case JSON_BOOL:
        break;
    case JSON_NUMBER:
        free(v->u.number.bytes);
        break;
    case JSON_STRING:
        free(v->u.string.bytes);
        break;
    case JSON_ARRAY:
        for (i = 0; i < v->u.array.n; i++) {
            free_value(v->u.array.items[i]);
        }
        free(v->u.array.items);
        break;
    case JSON_OBJECT:
        free_members(v->u.object.members, v->u.object.n);
        break;
    default:
        break;
    }
    free(v);
}

static bool hex_nibble(unsigned char c, unsigned int *out)
{
    if (c >= '0' && c <= '9') {
        *out = (unsigned int)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = (unsigned int)(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (unsigned int)(c - 'A' + 10);
        return true;
    }
    return false;
}

static bool read_u4(Parser *p, unsigned int *out)
{
    unsigned int v = 0;
    int i = 0;

    for (i = 0; i < 4; i++) {
        unsigned int nib = 0;
        unsigned char c = 0;

        if (at_end(p)) {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return false;
        }
        c = next(p);
        if (!hex_nibble(c, &nib)) {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return false;
        }
        v = (v << 4) | nib;
    }
    *out = v;
    return true;
}

static bool is_surrogate(unsigned int cp)
{
    return cp >= 0xD800U && cp <= 0xDFFFU;
}

static bool append_byte(char **buf, size_t *n, size_t *cap, unsigned char b, Parser *p)
{
    if (*n + 1U >= *cap) {
        size_t next_cap = (*cap == 0U) ? 16U : (*cap * 2U);
        char *grown = (char *)realloc(*buf, next_cap);

        if (grown == NULL) {
            set_err(p, JSONREAD_ERR_OOM);
            return false;
        }
        *buf = grown;
        *cap = next_cap;
    }
    (*buf)[(*n)++] = (char)b;
    return true;
}

static bool append_utf8(char **buf, size_t *n, size_t *cap, unsigned int cp, Parser *p)
{
    if (cp <= 0x7FU) {
        return append_byte(buf, n, cap, (unsigned char)cp, p);
    }
    if (cp <= 0x7FFU) {
        return append_byte(buf, n, cap, (unsigned char)(0xC0U | (cp >> 6)), p) &&
               append_byte(buf, n, cap, (unsigned char)(0x80U | (cp & 0x3FU)), p);
    }
    if (cp <= 0xFFFFU) {
        if (is_surrogate(cp)) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        return append_byte(buf, n, cap, (unsigned char)(0xE0U | (cp >> 12)), p) &&
               append_byte(buf, n, cap, (unsigned char)(0x80U | ((cp >> 6) & 0x3FU)), p) &&
               append_byte(buf, n, cap, (unsigned char)(0x80U | (cp & 0x3FU)), p);
    }
    if (cp <= 0x10FFFFU) {
        return append_byte(buf, n, cap, (unsigned char)(0xF0U | (cp >> 18)), p) &&
               append_byte(buf, n, cap, (unsigned char)(0x80U | ((cp >> 12) & 0x3FU)), p) &&
               append_byte(buf, n, cap, (unsigned char)(0x80U | ((cp >> 6) & 0x3FU)), p) &&
               append_byte(buf, n, cap, (unsigned char)(0x80U | (cp & 0x3FU)), p);
    }
    set_err(p, JSONREAD_ERR_UTF8);
    return false;
}

/* Decode one well-formed UTF-8 scalar from p->src at p->at. Advances p. */
static bool take_utf8_scalar(Parser *p, unsigned int *cp_out)
{
    unsigned char b0 = 0;
    unsigned char b1 = 0;
    unsigned char b2 = 0;
    unsigned char b3 = 0;
    unsigned int cp = 0;

    if (at_end(p)) {
        set_err(p, JSONREAD_ERR_UTF8);
        return false;
    }
    b0 = next(p);
    if (b0 < 0x80U) {
        *cp_out = b0;
        return true;
    }
    if ((b0 & 0xE0U) == 0xC0U) {
        if (at_end(p) || b0 < 0xC2U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        b1 = next(p);
        if ((b1 & 0xC0U) != 0x80U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        cp = ((unsigned int)(b0 & 0x1FU) << 6) | (unsigned int)(b1 & 0x3FU);
        *cp_out = cp;
        return true;
    }
    if ((b0 & 0xF0U) == 0xE0U) {
        if (p->at + 1U >= p->len) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        b1 = next(p);
        b2 = next(p);
        if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        if (b0 == 0xE0U && b1 < 0xA0U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        if (b0 == 0xEDU && b1 >= 0xA0U) {
            set_err(p, JSONREAD_ERR_UTF8); /* UTF-8 encoded surrogate */
            return false;
        }
        cp = ((unsigned int)(b0 & 0x0FU) << 12) | ((unsigned int)(b1 & 0x3FU) << 6) |
             (unsigned int)(b2 & 0x3FU);
        *cp_out = cp;
        return true;
    }
    if ((b0 & 0xF8U) == 0xF0U) {
        if (p->at + 2U >= p->len || b0 > 0xF4U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        b1 = next(p);
        b2 = next(p);
        b3 = next(p);
        if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U || (b3 & 0xC0U) != 0x80U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        if (b0 == 0xF0U && b1 < 0x90U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        if (b0 == 0xF4U && b1 >= 0x90U) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        cp = ((unsigned int)(b0 & 0x07U) << 18) | ((unsigned int)(b1 & 0x3FU) << 12) |
             ((unsigned int)(b2 & 0x3FU) << 6) | (unsigned int)(b3 & 0x3FU);
        *cp_out = cp;
        return true;
    }
    set_err(p, JSONREAD_ERR_UTF8);
    return false;
}

static bool parse_string_into(Parser *p, char **out_bytes, size_t *out_len)
{
    char *buf = NULL;
    size_t n = 0;
    size_t cap = 0;

    if (next(p) != '"') {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return false;
    }
    while (!at_end(p)) {
        unsigned char c = peek(p);

        if (c == '"') {
            (void)next(p);
            if (!append_byte(&buf, &n, &cap, '\0', p)) {
                free(buf);
                return false;
            }
            *out_bytes = buf;
            *out_len = n - 1U; /* exclude the NUL */
            return true;
        }
        if (c == '\\') {
            unsigned char esc = 0;
            unsigned int cp = 0;

            (void)next(p);
            if (at_end(p)) {
                set_err(p, JSONREAD_ERR_SYNTAX);
                free(buf);
                return false;
            }
            esc = next(p);
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                if (!append_byte(&buf, &n, &cap, esc, p)) {
                    free(buf);
                    return false;
                }
                break;
            case 'b':
                if (!append_byte(&buf, &n, &cap, '\b', p)) {
                    free(buf);
                    return false;
                }
                break;
            case 'f':
                if (!append_byte(&buf, &n, &cap, '\f', p)) {
                    free(buf);
                    return false;
                }
                break;
            case 'n':
                if (!append_byte(&buf, &n, &cap, '\n', p)) {
                    free(buf);
                    return false;
                }
                break;
            case 'r':
                if (!append_byte(&buf, &n, &cap, '\r', p)) {
                    free(buf);
                    return false;
                }
                break;
            case 't':
                if (!append_byte(&buf, &n, &cap, '\t', p)) {
                    free(buf);
                    return false;
                }
                break;
            case 'u':
                if (!read_u4(p, &cp)) {
                    free(buf);
                    return false;
                }
                if (cp >= 0xD800U && cp <= 0xDBFFU) {
                    unsigned int low = 0;

                    if (peek(p) != '\\') {
                        set_err(p, JSONREAD_ERR_UTF8);
                        free(buf);
                        return false;
                    }
                    (void)next(p);
                    if (next(p) != 'u') {
                        set_err(p, JSONREAD_ERR_UTF8);
                        free(buf);
                        return false;
                    }
                    if (!read_u4(p, &low)) {
                        free(buf);
                        return false;
                    }
                    if (low < 0xDC00U || low > 0xDFFFU) {
                        set_err(p, JSONREAD_ERR_UTF8);
                        free(buf);
                        return false;
                    }
                    cp = 0x10000U + (((cp - 0xD800U) << 10) | (low - 0xDC00U));
                } else if (is_surrogate(cp)) {
                    set_err(p, JSONREAD_ERR_UTF8);
                    free(buf);
                    return false;
                }
                if (!append_utf8(&buf, &n, &cap, cp, p)) {
                    free(buf);
                    return false;
                }
                break;
            default:
                set_err(p, JSONREAD_ERR_SYNTAX);
                free(buf);
                return false;
            }
            continue;
        }
        if (c < 0x20U) {
            set_err(p, JSONREAD_ERR_SYNTAX);
            free(buf);
            return false;
        }
        if (c < 0x80U) {
            (void)next(p);
            if (!append_byte(&buf, &n, &cap, c, p)) {
                free(buf);
                return false;
            }
        } else {
            unsigned int cp = 0;

            if (!take_utf8_scalar(p, &cp) || !append_utf8(&buf, &n, &cap, cp, p)) {
                free(buf);
                return false;
            }
        }
    }
    set_err(p, JSONREAD_ERR_SYNTAX);
    free(buf);
    return false;
}

static JsonValue *parse_value(Parser *p);

static bool expect_lit(Parser *p, const char *lit)
{
    size_t i = 0;

    for (i = 0; lit[i] != '\0'; i++) {
        if (at_end(p) || next(p) != (unsigned char)lit[i]) {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return false;
        }
    }
    return true;
}

static JsonValue *parse_number(Parser *p, int line)
{
    size_t start = p->at;
    size_t end = 0;
    char *copy = NULL;
    JsonValue *v = NULL;
    unsigned char c = 0;

    if (peek(p) == '-') {
        (void)next(p);
    }
    c = peek(p);
    if (c == '0') {
        (void)next(p);
    } else if (c >= '1' && c <= '9') {
        do {
            (void)next(p);
            c = peek(p);
        } while (c >= '0' && c <= '9');
    } else {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return NULL;
    }
    if (peek(p) == '.') {
        (void)next(p);
        c = peek(p);
        if (c < '0' || c > '9') {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return NULL;
        }
        do {
            (void)next(p);
            c = peek(p);
        } while (c >= '0' && c <= '9');
    }
    c = peek(p);
    if (c == 'e' || c == 'E') {
        (void)next(p);
        c = peek(p);
        if (c == '+' || c == '-') {
            (void)next(p);
            c = peek(p);
        }
        if (c < '0' || c > '9') {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return NULL;
        }
        do {
            (void)next(p);
            c = peek(p);
        } while (c >= '0' && c <= '9');
    }
    /* NaN / Infinity never match the grammar above; a leading letter fails. */
    end = p->at;
    copy = (char *)malloc(end - start + 1U);
    if (copy == NULL) {
        set_err(p, JSONREAD_ERR_OOM);
        return NULL;
    }
    memcpy(copy, p->src + start, end - start);
    copy[end - start] = '\0';
    v = alloc_value(p, JSON_NUMBER, line);
    if (v == NULL) {
        free(copy);
        return NULL;
    }
    v->u.number.bytes = copy;
    v->u.number.len = end - start;
    return v;
}

static bool grow_ptr_array(void ***items, size_t *n, size_t *cap, void *item, Parser *p)
{
    if (*n + 1U > *cap) {
        size_t next_cap = (*cap == 0U) ? 4U : (*cap * 2U);
        void **grown = (void **)realloc(*items, next_cap * sizeof(void *));

        if (grown == NULL) {
            set_err(p, JSONREAD_ERR_OOM);
            return false;
        }
        *items = grown;
        *cap = next_cap;
    }
    (*items)[(*n)++] = item;
    return true;
}

static bool grow_members(JsonMember **members, size_t *n, size_t *cap, JsonMember m, Parser *p)
{
    if (*n + 1U > *cap) {
        size_t next_cap = (*cap == 0U) ? 4U : (*cap * 2U);
        JsonMember *grown = (JsonMember *)realloc(*members, next_cap * sizeof(JsonMember));

        if (grown == NULL) {
            set_err(p, JSONREAD_ERR_OOM);
            return false;
        }
        *members = grown;
        *cap = next_cap;
    }
    (*members)[(*n)++] = m;
    return true;
}

static JsonValue *parse_array(Parser *p, int line)
{
    JsonValue *v = NULL;
    JsonValue **items = NULL;
    size_t n = 0;
    size_t cap = 0;

    if (p->depth > JSONREAD_NESTING_MAX) {
        set_err(p, JSONREAD_ERR_NESTING);
        return NULL;
    }
    (void)next(p); /* '[' */
    skip_ws(p);
    if (peek(p) == ']') {
        (void)next(p);
        v = alloc_value(p, JSON_ARRAY, line);
        if (v == NULL) {
            return NULL;
        }
        return v;
    }
    for (;;) {
        JsonValue *item = NULL;

        skip_ws(p);
        item = parse_value(p);
        if (item == NULL) {
            for (size_t i = 0; i < n; i++) {
                free_value(items[i]);
            }
            free(items);
            return NULL;
        }
        if (!grow_ptr_array((void ***)&items, &n, &cap, item, p)) {
            free_value(item);
            for (size_t i = 0; i < n; i++) {
                free_value(items[i]);
            }
            free(items);
            return NULL;
        }
        skip_ws(p);
        if (peek(p) == ',') {
            (void)next(p);
            continue;
        }
        if (peek(p) == ']') {
            (void)next(p);
            break;
        }
        set_err(p, JSONREAD_ERR_SYNTAX);
        for (size_t i = 0; i < n; i++) {
            free_value(items[i]);
        }
        free(items);
        return NULL;
    }
    v = alloc_value(p, JSON_ARRAY, line);
    if (v == NULL) {
        for (size_t i = 0; i < n; i++) {
            free_value(items[i]);
        }
        free(items);
        return NULL;
    }
    v->u.array.items = items;
    v->u.array.n = n;
    return v;
}

static JsonValue *parse_object(Parser *p, int line)
{
    JsonValue *v = NULL;
    JsonMember *members = NULL;
    size_t n = 0;
    size_t cap = 0;

    if (p->depth > JSONREAD_NESTING_MAX) {
        set_err(p, JSONREAD_ERR_NESTING);
        return NULL;
    }
    (void)next(p); /* '{' */
    skip_ws(p);
    if (peek(p) == '}') {
        (void)next(p);
        return alloc_value(p, JSON_OBJECT, line);
    }
    for (;;) {
        JsonMember m = {NULL, 0, NULL};
        char *key = NULL;
        size_t key_len = 0;

        skip_ws(p);
        if (peek(p) != '"') {
            set_err(p, JSONREAD_ERR_SYNTAX);
            free_members(members, n);
            return NULL;
        }
        if (!parse_string_into(p, &key, &key_len)) {
            free_members(members, n);
            return NULL;
        }
        skip_ws(p);
        if (next(p) != ':') {
            set_err(p, JSONREAD_ERR_SYNTAX);
            free(key);
            free_members(members, n);
            return NULL;
        }
        skip_ws(p);
        m.key = key;
        m.key_len = key_len;
        m.value = parse_value(p);
        if (m.value == NULL) {
            free(key);
            free_members(members, n);
            return NULL;
        }
        if (!grow_members(&members, &n, &cap, m, p)) {
            free(key);
            free_value(m.value);
            free_members(members, n);
            return NULL;
        }
        skip_ws(p);
        if (peek(p) == ',') {
            (void)next(p);
            continue;
        }
        if (peek(p) == '}') {
            (void)next(p);
            break;
        }
        set_err(p, JSONREAD_ERR_SYNTAX);
        free_members(members, n);
        return NULL;
    }
    v = alloc_value(p, JSON_OBJECT, line);
    if (v == NULL) {
        free_members(members, n);
        return NULL;
    }
    v->u.object.members = members;
    v->u.object.n = n;
    return v;
}

static JsonValue *parse_value(Parser *p)
{
    int line = 0;
    unsigned char c = 0;
    JsonValue *v = NULL;

    skip_ws(p);
    if (at_end(p)) {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return NULL;
    }
    line = p->line;
    c = peek(p);
    if (c == 'n') {
        if (!expect_lit(p, "null")) {
            return NULL;
        }
        return alloc_value(p, JSON_NULL, line);
    }
    if (c == 't') {
        if (!expect_lit(p, "true")) {
            return NULL;
        }
        v = alloc_value(p, JSON_BOOL, line);
        if (v != NULL) {
            v->u.boolean = true;
        }
        return v;
    }
    if (c == 'f') {
        if (!expect_lit(p, "false")) {
            return NULL;
        }
        v = alloc_value(p, JSON_BOOL, line);
        if (v != NULL) {
            v->u.boolean = false;
        }
        return v;
    }
    if (c == '"') {
        char *bytes = NULL;
        size_t len = 0;

        if (!parse_string_into(p, &bytes, &len)) {
            return NULL;
        }
        v = alloc_value(p, JSON_STRING, line);
        if (v == NULL) {
            free(bytes);
            return NULL;
        }
        v->u.string.bytes = bytes;
        v->u.string.len = len;
        return v;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return parse_number(p, line);
    }
    if (c == '[') {
        p->depth++;
        v = parse_array(p, line);
        p->depth--;
        return v;
    }
    if (c == '{') {
        p->depth++;
        v = parse_object(p, line);
        p->depth--;
        return v;
    }
    /* Bare NaN / Infinity / +Infinity start with a letter or '+' — syntax. */
    set_err(p, JSONREAD_ERR_SYNTAX);
    return NULL;
}

JsonReadStatus jsonread_parse(const char *src, size_t len, JsonValue **out)
{
    Parser p;
    JsonValue *root = NULL;

    if (out == NULL) {
        return JSONREAD_ERR_INTERNAL;
    }
    *out = NULL;
    if (src == NULL && len != 0U) {
        return JSONREAD_ERR_INTERNAL;
    }
    memset(&p, 0, sizeof(p));
    p.src = (src != NULL) ? src : "";
    p.len = len;
    p.line = 1;
    p.err = JSONREAD_OK;
    root = parse_value(&p);
    if (root == NULL) {
        return (p.err != JSONREAD_OK) ? p.err : JSONREAD_ERR_SYNTAX;
    }
    skip_ws(&p);
    if (!at_end(&p)) {
        free_value(root);
        return JSONREAD_ERR_SYNTAX; /* trailing garbage */
    }
    *out = root;
    return JSONREAD_OK;
}

void jsonread_free(JsonValue *v)
{
    free_value(v);
}

const char *jsonread_status_string(JsonReadStatus st)
{
    switch (st) {
    case JSONREAD_OK:
        return "ok";
    case JSONREAD_ERR_SYNTAX:
        return "syntax";
    case JSONREAD_ERR_UTF8:
        return "utf8";
    case JSONREAD_ERR_NESTING:
        return "nesting";
    case JSONREAD_ERR_OOM:
        return "oom";
    case JSONREAD_ERR_INTERNAL:
        return "internal";
    default:
        return "internal";
    }
}
