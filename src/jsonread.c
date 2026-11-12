#include "jsonread.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "normalize.h"
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
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        return true;
    }
    return false;
}

static void skip_ws(Parser *p)
{
    while (!at_end(p) && is_ws(peek(p))) {
        (void)next(p);
    }
}

/* type before p so the enum and the int line are never adjacent. */
static JsonValue *alloc_value(JsonType type, Parser *p, int line)
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

/* NOLINTNEXTLINE(misc-no-recursion) — depth is capped at JSONREAD_NESTING_MAX */
static void free_items(JsonValue **items, size_t n)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        free_value(items[i]);
    }
    free((void *)items);
}

/* NOLINTNEXTLINE(misc-no-recursion) — depth is capped at JSONREAD_NESTING_MAX */
static void free_members(JsonMember *members, size_t n)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        free(members[i].key);
        free_value(members[i].value);
    }
    free(members);
}

/* NOLINTNEXTLINE(misc-no-recursion) — depth is capped at JSONREAD_NESTING_MAX */
static void free_value(JsonValue *v)
{
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
        free_items(v->u.array.items, v->u.array.n);
        break;
    case JSON_OBJECT:
        free_members(v->u.object.members, v->u.object.n);
        break;
    default:
        break;
    }
    free(v);
}

/* ---- strings ------------------------------------------------------------- */

/* Unsigned so the shifts and masks stay unsigned (hicpp-signed-bitwise). */
static const unsigned HEX_LETTER_BASE = 10U;
static const unsigned HEX_SHIFT = 4U;
static const unsigned FIRST_PRINTABLE = 0x20U;
static const unsigned CP_1BYTE_MAX = 0x7FU;
static const unsigned CP_2BYTE_MAX = 0x7FFU;
static const unsigned CP_3BYTE_MAX = 0xFFFFU;
static const unsigned CP_HIGH_MIN = 0xD800U;
static const unsigned CP_HIGH_MAX = 0xDBFFU;
static const unsigned CP_LOW_MIN = 0xDC00U;
static const unsigned CP_LOW_MAX = 0xDFFFU;
static const unsigned CP_PAIR_BASE = 0x10000U;
static const unsigned CP_PAIR_SHIFT = 10U;
static const unsigned UTF8_LEAD2_TAG = 0xC0U;
static const unsigned UTF8_LEAD3_TAG = 0xE0U;
static const unsigned UTF8_LEAD4_TAG = 0xF0U;
static const unsigned UTF8_CONT_TAG = 0x80U;
static const unsigned UTF8_CONT_PAYLOAD = 0x3FU;
static const unsigned UTF8_CONT_SHIFT = 6U;
static const size_t STRBUF_FIRST_CAP = 16U;
static const size_t ITEMS_FIRST_CAP = 4U;

typedef struct {
    char *bytes;
    size_t n;
    size_t cap;
} StrBuf;

static bool hex_nibble(unsigned char c, unsigned int *out)
{
    if (c >= '0' && c <= '9') {
        *out = (unsigned int)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = (unsigned int)(c - 'a') + HEX_LETTER_BASE;
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (unsigned int)(c - 'A') + HEX_LETTER_BASE;
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

        if (at_end(p) || !hex_nibble(next(p), &nib)) {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return false;
        }
        v = (v << HEX_SHIFT) | nib;
    }
    *out = v;
    return true;
}

static bool append_byte(StrBuf *b, unsigned char c, Parser *p)
{
    if (b->n + 1U >= b->cap) {
        size_t next_cap = (b->cap == 0U) ? STRBUF_FIRST_CAP : (b->cap * 2U);
        char *grown = (char *)realloc(b->bytes, next_cap);

        if (grown == NULL) {
            set_err(p, JSONREAD_ERR_OOM);
            return false;
        }
        b->bytes = grown;
        b->cap = next_cap;
    }
    b->bytes[b->n++] = (char)c;
    return true;
}

/* Encode a scalar from a \u escape. The caller has already refused
   surrogates and NUL, and a pair is at most U+10FFFF. */
static bool append_utf8(StrBuf *b, unsigned int cp, Parser *p)
{
    unsigned char out[4];
    size_t len = 0;
    size_t i = 0;
    unsigned lead = 0;

    if (cp <= CP_1BYTE_MAX) {
        return append_byte(b, (unsigned char)cp, p);
    }
    if (cp <= CP_2BYTE_MAX) {
        len = 2;
        lead = UTF8_LEAD2_TAG;
    } else if (cp <= CP_3BYTE_MAX) {
        len = 3;
        lead = UTF8_LEAD3_TAG;
    } else {
        len = 4;
        lead = UTF8_LEAD4_TAG;
    }
    for (i = len - 1U; i > 0U; i--) {
        out[i] = (unsigned char)(UTF8_CONT_TAG | (cp & UTF8_CONT_PAYLOAD));
        cp >>= UTF8_CONT_SHIFT;
    }
    out[0] = (unsigned char)(lead | cp);
    for (i = 0; i < len; i++) {
        if (!append_byte(b, out[i], p)) {
            return false;
        }
    }
    return true;
}

/* The byte a one-letter escape stands for, or false for an unknown letter. */
static bool simple_escape(unsigned char esc, unsigned char *out)
{
    static const char from[] = "\"\\/bfnrt";
    static const char to[] = "\"\\/\b\f\n\r\t";
    const char *hit = (esc != 0U) ? strchr(from, (int)esc) : NULL;

    if (hit == NULL) {
        return false;
    }
    *out = (unsigned char)to[hit - from];
    return true;
}

/* The scalar after "\u": one escape, or a high and low surrogate pair. */
static bool take_u_escape(Parser *p, unsigned int *cp_out)
{
    unsigned int cp = 0;
    unsigned int low = 0;

    if (!read_u4(p, &cp)) {
        return false;
    }
    if (cp >= CP_HIGH_MIN && cp <= CP_HIGH_MAX) {
        if (peek(p) != '\\') {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        (void)next(p);
        if (next(p) != 'u') {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        if (!read_u4(p, &low)) {
            return false;
        }
        if (low < CP_LOW_MIN || low > CP_LOW_MAX) {
            set_err(p, JSONREAD_ERR_UTF8);
            return false;
        }
        cp = CP_PAIR_BASE + (((cp - CP_HIGH_MIN) << CP_PAIR_SHIFT) | (low - CP_LOW_MIN));
    } else if (cp >= CP_HIGH_MIN && cp <= CP_LOW_MAX) {
        set_err(p, JSONREAD_ERR_UTF8); /* a lone low surrogate */
        return false;
    } else if (cp == 0U) {
        /* A NUL would cut every C string short
           (decision:plan23-review-g3-nul-is-import-parse). */
        set_err(p, JSONREAD_ERR_SYNTAX);
        return false;
    }
    *cp_out = cp;
    return true;
}

/* One escape after the backslash, appended to b. */
static bool take_escape(Parser *p, StrBuf *b)
{
    unsigned char esc = 0;
    unsigned char plain = 0;
    unsigned int cp = 0;

    if (at_end(p)) {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return false;
    }
    esc = next(p);
    if (esc == 'u') {
        if (!take_u_escape(p, &cp)) {
            return false;
        }
        return append_utf8(b, cp, p);
    }
    if (!simple_escape(esc, &plain)) {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return false;
    }
    return append_byte(b, plain, p);
}

/* The whole input is valid UTF-8 (jsonread_parse checks it first), so a
   byte at or above 0x80 is copied as it is. */
static bool parse_string_into(Parser *p, char **out_bytes, size_t *out_len)
{
    StrBuf b = {NULL, 0, 0};

    if (next(p) != '"') {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return false;
    }
    while (!at_end(p)) {
        unsigned char c = next(p);
        bool done = false;

        if (c == '"') {
            if (!append_byte(&b, '\0', p)) {
                break;
            }
            *out_bytes = b.bytes;
            *out_len = b.n - 1U; /* exclude the NUL */
            return true;
        }
        if (c == '\\') {
            done = take_escape(p, &b);
        } else if (c < FIRST_PRINTABLE) {
            set_err(p, JSONREAD_ERR_SYNTAX);
        } else {
            done = append_byte(&b, c, p);
        }
        if (!done) {
            free(b.bytes);
            return false;
        }
    }
    set_err(p, JSONREAD_ERR_SYNTAX);
    free(b.bytes);
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

/* Skip a run of ASCII digits; the count it skipped. */
static size_t skip_digits(Parser *p)
{
    size_t n = 0;

    while (peek(p) >= '0' && peek(p) <= '9') {
        (void)next(p);
        n++;
    }
    return n;
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
    if (peek(p) == '0') {
        (void)next(p);
    } else if (skip_digits(p) == 0U) {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return NULL;
    }
    if (peek(p) == '.') {
        (void)next(p);
        if (skip_digits(p) == 0U) {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return NULL;
        }
    }
    c = peek(p);
    if (c == 'e' || c == 'E') {
        (void)next(p);
        c = peek(p);
        if (c == '+' || c == '-') {
            (void)next(p);
        }
        if (skip_digits(p) == 0U) {
            set_err(p, JSONREAD_ERR_SYNTAX);
            return NULL;
        }
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
    v = alloc_value(JSON_NUMBER, p, line);
    if (v == NULL) {
        free(copy);
        return NULL;
    }
    v->u.number.bytes = copy;
    v->u.number.len = end - start;
    return v;
}

static bool grow_items(JsonValue ***items, size_t *n, size_t *cap, JsonValue *item, Parser *p)
{
    if (*n + 1U > *cap) {
        size_t next_cap = (*cap == 0U) ? ITEMS_FIRST_CAP : (*cap * 2U);
        JsonValue **grown = (JsonValue **)realloc((void *)*items, next_cap * sizeof(JsonValue *));

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
        size_t next_cap = (*cap == 0U) ? ITEMS_FIRST_CAP : (*cap * 2U);
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

/* NOLINTNEXTLINE(misc-no-recursion) — depth is capped at JSONREAD_NESTING_MAX */
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
        v = alloc_value(JSON_ARRAY, p, line);
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
            free_items(items, n);
            return NULL;
        }
        if (!grow_items(&items, &n, &cap, item, p)) {
            free_value(item);
            free_items(items, n);
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
        free_items(items, n);
        return NULL;
    }
    v = alloc_value(JSON_ARRAY, p, line);
    if (v == NULL) {
        free_items(items, n);
        return NULL;
    }
    v->u.array.items = items;
    v->u.array.n = n;
    return v;
}

/* NOLINTNEXTLINE(misc-no-recursion) — depth is capped at JSONREAD_NESTING_MAX */
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
        return alloc_value(JSON_OBJECT, p, line);
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
    v = alloc_value(JSON_OBJECT, p, line);
    if (v == NULL) {
        free_members(members, n);
        return NULL;
    }
    v->u.object.members = members;
    v->u.object.n = n;
    return v;
}

static JsonValue *parse_value_at(Parser *p, int line);

/* One value plus its source byte span (callers such as the nested settings
   document re-read those exact bytes). */
/* NOLINTNEXTLINE(misc-no-recursion) — depth is capped at JSONREAD_NESTING_MAX */
static JsonValue *parse_value(Parser *p)
{
    size_t start = 0;
    JsonValue *v = NULL;

    skip_ws(p);
    if (at_end(p)) {
        set_err(p, JSONREAD_ERR_SYNTAX);
        return NULL;
    }
    start = p->at;
    v = parse_value_at(p, p->line);
    if (v != NULL) {
        v->src_at = start;
        v->src_len = p->at - start;
    }
    return v;
}

/* NOLINTNEXTLINE(misc-no-recursion) — depth is capped at JSONREAD_NESTING_MAX */
static JsonValue *parse_value_at(Parser *p, int line)
{
    unsigned char c = 0;
    JsonValue *v = NULL;

    c = peek(p);
    if (c == 'n') {
        if (!expect_lit(p, "null")) {
            return NULL;
        }
        return alloc_value(JSON_NULL, p, line);
    }
    if (c == 't') {
        if (!expect_lit(p, "true")) {
            return NULL;
        }
        v = alloc_value(JSON_BOOL, p, line);
        if (v != NULL) {
            v->u.boolean = true;
        }
        return v;
    }
    if (c == 'f') {
        if (!expect_lit(p, "false")) {
            return NULL;
        }
        v = alloc_value(JSON_BOOL, p, line);
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
        v = alloc_value(JSON_STRING, p, line);
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
    if (!utf8_is_valid(src, len)) {
        return JSONREAD_ERR_UTF8;
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
    default:
        return "internal";
    }
}
