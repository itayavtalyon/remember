#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "store.h"
#include "util.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* list / search: shared filter + paging parse, different store call + query. */

enum { LIST_LIMIT_DEFAULT = 20 };
enum { LIST_LIMIT_MAX = 1000 };
/* Bounded well under the store's per-query parameter budget (LIST_BIND_CAP) so
   an over-cap filter set is a clear user error here, not an opaque store one. */
enum { LIST_TAG_FILTER_MAX = 50 };
enum { FACET_LIMIT_DEFAULT = 50, FACET_LIMIT_MAX = 200 }; /* E2.1 */

/* Parse a non-negative size_t from decimal text. Rejects empty, non-digits, ERANGE. */
static int parse_size_token(const char *raw, size_t *out)
{
    unsigned long long value = 0;

    if (util_parse_nonneg(raw, &value) != 0) {
        return -1;
    }
#if SIZE_MAX < ULLONG_MAX
    /* 32-bit size_t: reject rather than silently truncate the value. */
    if (value > (unsigned long long)SIZE_MAX) {
        return -1;
    }
#endif
    *out = (size_t)value;
    return 0;
}

typedef struct {
    const char *created_by;
    const char *updated_by;
    const char *key_raw;
    const char *sort_raw; /* NULL when --sort was omitted (default updated) */
    const char *since_raw;
    const char *until_raw;
    const char **tag_raw;
    size_t ntag_raw;
    size_t limit;
    const char *cursor; /* argv token; not owned. NULL = first page */
    CmdBinOpts bins;
} ListParse;

static void list_parse_free(ListParse *p)
{
    free((void *)p->tag_raw);
    p->tag_raw = NULL;
    p->ntag_raw = 0U;
}

/* One size option with its bounds 1..max and its two usage sentences. */
typedef struct {
    const char *missing;
    const char *invalid;
    size_t max;
} SizeOption;

static int take_size_option(int *i, int rest_argc, const char **rest_argv, SizeOption opt,
                            size_t *out, const char **err)
{
    size_t value = 0U;
    TakeValue taken = take_value(i, rest_argc, rest_argv, opt.missing);

    if (taken.rc != 0) {
        *err = taken.err;
        return -1;
    }
    if (parse_size_token(taken.value, &value) != 0 || value == 0U || value > opt.max) {
        *err = opt.invalid;
        return -1;
    }
    *out = value;
    return 0;
}

/* rest_argv and slot, and the two usage sentences, are adjacent on purpose. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static int list_take_raw(int *i, int rest_argc, const char **rest_argv, const char **slot,
                         const char *duplicate, const char *missing, const char **err)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    TakeValue taken;

    if (*slot != NULL) {
        *err = duplicate;
        return -1;
    }
    taken = take_value(i, rest_argc, rest_argv, missing);
    if (taken.rc != 0) {
        *err = taken.err;
        return -1;
    }
    *slot = taken.value;
    return 0;
}

static int list_take_sort(int *i, int rest_argc, const char **rest_argv, ListParse *out,
                          const char **err)
{
    if (list_take_raw(i, rest_argc, rest_argv, &out->sort_raw, "duplicate --sort",
                      "missing value for --sort", err) != 0) {
        return -1;
    }
    if (strcmp(out->sort_raw, "updated") != 0 && strcmp(out->sort_raw, "created") != 0) {
        *err = "invalid --sort (use updated or created)";
        return -1;
    }
    return 0;
}

/*
 * Handle one list/search option. Returns: 0 handled, 1 end-opts, 2 positional
 * for caller, -1 error (*err set; empty string if message already printed).
 * --cursor is a list flag. Search rejects it as an unknown option.
 */
static int list_handle_opt(const char *arg, int *i, int rest_argc, const char **rest_argv,
                           ListParse *out, size_t *tag_cap, bool allow_cursor, const char **err)
{
    const char *val = NULL;

    if (strcmp(arg, "--") == 0) {
        return 1;
    }
    if (strcmp(arg, "--tag") == 0) {
        {
            TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --tag");
            if (taken.rc != 0) {
                *err = taken.err;
                return -1;
            }
            val = taken.value;
        }
        if (push_cstr_ptr(&out->tag_raw, &out->ntag_raw, tag_cap, val) != 0) {
            *err = "out of memory";
            return -1;
        }
        return 0;
    }
    if (strcmp(arg, "--created-by") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --created-by");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->created_by = taken.value;
        return 0;
    }
    if (strcmp(arg, "--updated-by") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --updated-by");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->updated_by = taken.value;
        return 0;
    }
    if (strcmp(arg, "--key") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --key");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->key_raw = taken.value;
        return 0;
    }
    if (strcmp(arg, "--limit") == 0) {
        return take_size_option(i, rest_argc, rest_argv,
                                (SizeOption){.missing = "missing value for --limit",
                                             .invalid = "invalid --limit (must be 1..1000)",
                                             .max = LIST_LIMIT_MAX},
                                &out->limit, err);
    }
    if (strcmp(arg, "--cursor") == 0) {
        if (!allow_cursor) {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            *err = "";
            return -1;
        }
        return list_take_raw(i, rest_argc, rest_argv, &out->cursor, "duplicate --cursor",
                             "missing value for --cursor", err);
    }
    if (strcmp(arg, "--sort") == 0) {
        return list_take_sort(i, rest_argc, rest_argv, out, err);
    }
    if (strcmp(arg, "--since") == 0) {
        return list_take_raw(i, rest_argc, rest_argv, &out->since_raw, "duplicate --since",
                             "missing value for --since", err);
    }
    if (strcmp(arg, "--until") == 0) {
        return list_take_raw(i, rest_argc, rest_argv, &out->until_raw, "duplicate --until",
                             "missing value for --until", err);
    }
    if (cmd_bin_take_flag(arg, &out->bins) != 0) {
        return 0;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
        *err = "";
        return -1;
    }
    return 2;
}

static void list_parse_init(ListParse *out)
{
    out->created_by = NULL;
    out->updated_by = NULL;
    out->key_raw = NULL;
    out->sort_raw = NULL;
    out->since_raw = NULL;
    out->until_raw = NULL;
    out->tag_raw = NULL;
    out->ntag_raw = 0U;
    out->limit = LIST_LIMIT_DEFAULT;
    out->cursor = NULL;
    memset(&out->bins, 0, sizeof(out->bins));
}

static int parse_list_args(int rest_argc, const char **rest_argv, ListParse *out, const char **err)
{
    int i = 0;
    int end_opts = 0;
    size_t tag_cap = 0U;

    list_parse_init(out);
    *err = NULL;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];
        int kind = 0;

        if (end_opts) {
            *err = "unexpected argument";
            return -1;
        }
        kind = list_handle_opt(arg, &i, rest_argc, rest_argv, out, &tag_cap, true, err);
        if (kind == 1) {
            end_opts = 1;
            continue;
        }
        if (kind == 0) {
            continue;
        }
        if (kind < 0) {
            return -1;
        }
        *err = "unexpected argument";
        return -1;
    }
    return 0;
}

/*
 * Same filters/paging as list, plus exactly one required QUERY positional and
 * --facet-limit. QUERY goes to the store as given: the store owns E2.1's trim
 * and blank rule (D13).
 */
typedef struct {
    ListParse filters;
    const char *query; /* argv token; not owned */
    size_t facet_limit;
} SearchParse;

/* Accept the single positional QUERY; a second positional is a user error. */
static int search_set_query(SearchParse *out, const char *arg, const char **err)
{
    if (out->query != NULL) {
        *err = "unexpected argument";
        return -1;
    }
    out->query = arg;
    return 0;
}

static int parse_search_args(int rest_argc, const char **rest_argv, SearchParse *out,
                             const char **err)
{
    int i = 0;
    int end_opts = 0;
    size_t tag_cap = 0U;

    list_parse_init(&out->filters);
    out->query = NULL;
    out->facet_limit = FACET_LIMIT_DEFAULT;
    *err = NULL;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];
        int kind = 0;

        if (end_opts) {
            if (search_set_query(out, arg, err) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(arg, "--facet-limit") == 0) {
            if (take_size_option(&i, rest_argc, rest_argv,
                                 (SizeOption){.missing = "missing value for --facet-limit",
                                              .invalid = "invalid --facet-limit (must be 1..200)",
                                              .max = FACET_LIMIT_MAX},
                                 &out->facet_limit, err) != 0) {
                return -1;
            }
            continue;
        }
        kind = list_handle_opt(arg, &i, rest_argc, rest_argv, &out->filters, &tag_cap, false, err);
        if (kind == 1) {
            end_opts = 1;
            continue;
        }
        if (kind == 0) {
            continue;
        }
        if (kind < 0) {
            return -1;
        }
        /* Positional: the QUERY (exactly one). */
        if (search_set_query(out, arg, err) != 0) {
            return -1;
        }
    }
    if (out->query == NULL) {
        *err = "missing search query";
        return -1;
    }
    if (!utf8_is_valid(out->query, strlen(out->query))) {
        *err = "invalid UTF-8 in search query"; /* D27 */
        return -1;
    }
    return 0;
}

static int list_bind_bound(const char *raw, char out[ISO_TS_BUFSIZE], const char **bound)
{
    if (raw == NULL) {
        *bound = NULL;
        return 0;
    }
    if (parse_filter_ts(raw, out) != 0) {
        err_fmt(DIAG_USAGE, "invalid timestamp: %s", raw);
        return -1;
    }
    *bound = out;
    return 0;
}

/*
 * Validate parsed filters and normalize them into *q. Allocates *out_tags (heap;
 * caller frees with free_tag_list) that q->tags points into; key_norm is caller
 * storage that q->key points into. since_out/until_out hold canonical bounds
 * when those flags were set. Prints its own error and returns -1 on failure,
 * 0 on success.
 */
static int list_prepare_query(const ListParse *parsed, char *key_norm, size_t key_norm_sz,
                              char ***out_tags, size_t *out_ntags, ListQuery *q,
                              char since_out[ISO_TS_BUFSIZE], char until_out[ISO_TS_BUFSIZE])
{
    const char *err = NULL;

    *out_tags = NULL;
    *out_ntags = 0U;
    q->key = NULL; /* default; set below only when --key was given */

    if (cmd_reject_filter_kind("--created-by", parsed->created_by) != 0 ||
        cmd_reject_filter_kind("--updated-by", parsed->updated_by) != 0) {
        return -1;
    }
    if (parsed->ntag_raw > LIST_TAG_FILTER_MAX) {
        err_msg(DIAG_USAGE, "too many --tag filters (max 50)");
        return -1;
    }
    if (normalize_tags((const char *const *)parsed->tag_raw, parsed->ntag_raw, out_tags, out_ntags,
                       &err) != 0) {
        err_msg(code_for_cmd_text(err), err);
        return -1;
    }
    if (parsed->key_raw != NULL) {
        NormStatus ns = normalize_key(parsed->key_raw, key_norm, key_norm_sz);
        if (ns != NORM_OK) {
            err_norm(ns, "key");
            return -1;
        }
        q->key = key_norm;
    }
    q->tags = (const char *const *)*out_tags;
    q->ntags = *out_ntags;
    q->created_by = parsed->created_by;
    q->updated_by = parsed->updated_by;
    q->limit = parsed->limit;
    q->after_at = NULL;
    q->after_local_id = 0;
    q->sort = STORE_SORT_UPDATED;
    if (parsed->sort_raw != NULL && strcmp(parsed->sort_raw, "created") == 0) {
        q->sort = STORE_SORT_CREATED;
    }
    if (list_bind_bound(parsed->since_raw, since_out, &q->since) != 0 ||
        list_bind_bound(parsed->until_raw, until_out, &q->until) != 0) {
        return -1;
    }
    q->bin = STORE_BIN_LIVE;
    if (cmd_bin_resolve(&parsed->bins, &q->bin) != 0) {
        return -1;
    }
    return 0;
}

static int load_page_neighbors(Store *s, const Entry *entries, size_t count, const char *now,
                               StoreNeighbor **out, size_t *out_n)
{
    long long *ids = NULL;
    size_t i = 0;
    StoreStatus st = STORE_OK;

    *out = NULL;
    *out_n = 0U;
    if (count == 0U) {
        return 0;
    }
    ids = (long long *)malloc(count * sizeof(*ids));
    if (ids == NULL) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return -1;
    }
    for (i = 0; i < count; i++) {
        ids[i] = entries[i].id;
    }
    st = store_list_neighbors_for(s, ids, count, now, out, out_n);
    free(ids);
    if (st != STORE_OK) {
        err_store(s, st);
        return -1;
    }
    return 0;
}

/* Emit list/search page: JSON data or human preview lines. */
static int emit_entry_page(bool json, size_t count, size_t total, const Entry *entries,
                           const StoreNeighbor *links, size_t nlinks, const char *now)
{
    size_t i = 0;

    if (json) {
        return output_list_data(app_out(), count, total, entries, links, nlinks, now);
    }
    for (i = 0; i < count; i++) {
        if (output_entry_human_line(app_out(), &entries[i], links, nlinks, now) != 0) {
            return -1;
        }
    }
    return 0;
}

/* search: the E2.6 data object, or the E2.7 facet lines before the entry lines. */
static int emit_search(bool json, const SearchQuery *q, const SearchResult *res,
                       const StoreNeighbor *links, size_t nlinks, const char *now)
{
    if (json) {
        return output_search_data(app_out(), q, res, links, nlinks, now);
    }
    if (output_search_facets_human(app_out(), res) != 0) {
        return -1;
    }
    return emit_entry_page(false, res->page.count, res->page.total, res->page.entries, links,
                           nlinks, now);
}

static void free_entry_page(Entry *entries, size_t count)
{
    size_t i = 0;

    if (entries == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        store_entry_free(&entries[i]);
    }
    free(entries);
}

/* Opaque list cursor: unpadded base64url (RFC 4648 section 5) of one canonical
   JSON object (decision:plan21-cursor-binds-whole-query). Field order is fixed.
   Absent filters are omitted. --limit stays a flag. */

/* OOM is literal -1 so an untested malloc miss stays a defensive return. */
enum { CURSOR_OK = 0, CURSOR_OOM = -1, CURSOR_BAD = -2 };
enum { CURSOR_JSON_MAX = 65536, CURSOR_TAG_MAX = 50 };
enum { CURSOR_BUF_INIT = 16, HEX_LETTER_BASE = 10, JSON_MAX_UNIT = 0x7F, DECIMAL_BASE = 10 };

typedef struct {
    char *sort;
    char *bin;
    char *tags[CURSOR_TAG_MAX];
    size_t ntags;
    char *created_by;
    char *updated_by;
    char *key;
    char *since;
    char *until;
    char *at;
    long long local_id;
} CursorDoc;

static void cursor_doc_free(CursorDoc *doc)
{
    size_t i = 0U;

    if (doc == NULL) {
        return;
    }
    free(doc->sort);
    free(doc->bin);
    free(doc->created_by);
    free(doc->updated_by);
    free(doc->key);
    free(doc->since);
    free(doc->until);
    free(doc->at);
    for (i = 0U; i < doc->ntags; i++) {
        free(doc->tags[i]);
    }
    memset(doc, 0, sizeof(*doc));
}

/* Alphabet offsets and the 6-bit shifts are the base64url layout. */
// NOLINTBEGIN(hicpp-signed-bitwise,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
static int b64_digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (int)(c - 'A');
    }
    if (c >= 'a' && c <= 'z') {
        return (int)(c - 'a') + 26;
    }
    if (c >= '0' && c <= '9') {
        return (int)(c - '0') + 52;
    }
    if (c == '-') {
        return 62;
    }
    if (c == '_') {
        return 63;
    }
    return -1;
}

static char *base64url_encode(const unsigned char *raw, size_t len)
{
    static const char k_alpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char *out = (char *)malloc((4U * ((len + 2U) / 3U)) + 1U);
    size_t i = 0U;
    size_t j = 0U;

    if (out == NULL) {
        return NULL;
    }
    for (i = 0U; i < len; i += 3U) {
        size_t left = len - i;
        unsigned n = (unsigned)raw[i] << 16;

        if (left > 1U) {
            n |= (unsigned)raw[i + 1U] << 8;
        }
        if (left > 2U) {
            n |= (unsigned)raw[i + 2U];
        }
        out[j++] = k_alpha[(n >> 18) & 63U];
        out[j++] = k_alpha[(n >> 12) & 63U];
        if (left > 1U) {
            out[j++] = k_alpha[(n >> 6) & 63U];
        }
        if (left > 2U) {
            out[j++] = k_alpha[n & 63U];
        }
    }
    out[j] = '\0';
    return out;
}

/* Rejects any byte outside the alphabet (padding too). A short last group is
   taken as is: list_query_from_cursor re-encodes and accepts only the same
   bytes, so a stray length or non-zero unused bits fail there. */
static int base64url_decode(const char *text, unsigned char **out, size_t *out_len)
{
    size_t n = strlen(text);
    size_t i = 0U;
    size_t j = 0U;
    unsigned char *buf = NULL;

    if (n == 0U || ((n / 4U) + 1U) * 3U > (size_t)CURSOR_JSON_MAX) {
        return CURSOR_BAD;
    }
    buf = (unsigned char *)malloc((((n / 4U) + 1U) * 3U) + 1U);
    if (buf == NULL) {
        return -1;
    }
    for (i = 0U; i < n; i += 4U) {
        size_t chars = (n - i < 4U) ? n - i : 4U;
        unsigned value = 0U;
        size_t k = 0U;

        for (k = 0U; k < 4U; k++) {
            int digit = 0;

            if (k < chars) {
                digit = b64_digit((unsigned char)text[i + k]);
            }
            if (digit < 0) {
                free(buf);
                return CURSOR_BAD;
            }
            value = (value << 6) | (unsigned)digit;
        }
        buf[j++] = (unsigned char)((value >> 16) & 0xFFU);
        if (chars > 2U) {
            buf[j++] = (unsigned char)((value >> 8) & 0xFFU);
        }
        if (chars > 3U) {
            buf[j++] = (unsigned char)(value & 0xFFU);
        }
    }
    buf[j] = 0U;
    *out = buf;
    *out_len = j;
    return CURSOR_OK;
}
// NOLINTEND(hicpp-signed-bitwise,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

static int buf_add(char **buf, size_t *n, size_t *cap, char c)
{
    if (*n + 1U >= *cap) {
        size_t ncap = (*cap == 0U) ? (size_t)CURSOR_BUF_INIT : (*cap * 2U);
        char *grown = (char *)realloc(*buf, ncap);

        if (grown == NULL) {
            return -1;
        }
        *buf = grown;
        *cap = ncap;
    }
    (*buf)[(*n)++] = c;
    (*buf)[*n] = '\0';
    return CURSOR_OK;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + HEX_LETTER_BASE;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + HEX_LETTER_BASE;
    }
    return -1;
}

/* One JSON escape, after the backslash. Rejects \\u above ASCII. */
static int json_escape(const char **s, unsigned char *out)
{
    const char *p = *s;

    if (*p == '"' || *p == '\\' || *p == '/') {
        *out = (unsigned char)*p;
    } else if (*p == 'b') {
        *out = (unsigned char)'\b';
    } else if (*p == 'f') {
        *out = (unsigned char)'\f';
    } else if (*p == 'n') {
        *out = (unsigned char)'\n';
    } else if (*p == 'r') {
        *out = (unsigned char)'\r';
    } else if (*p == 't') {
        *out = (unsigned char)'\t';
    } else if (*p == 'u') {
        unsigned unit = 0U;
        int k = 0;

        for (k = 0; k < 4; k++) {
            int nib = hex_nibble(p[1 + k]);

            if (nib < 0) {
                return CURSOR_BAD;
            }
            unit = (unit << 4U) | (unsigned)nib;
        }
        if (unit > (unsigned)JSON_MAX_UNIT) {
            return CURSOR_BAD;
        }
        *out = (unsigned char)unit;
        p += 4;
    } else {
        return CURSOR_BAD;
    }
    p++;
    *s = p;
    return CURSOR_OK;
}

static int parse_json_string(const char **p, char **out)
{
    char *buf = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    const char *s = *p;

    *out = NULL;
    if (*s != '"') {
        return CURSOR_BAD;
    }
    s++;
    for (;;) {
        unsigned char c = 0;
        int st = CURSOR_OK;

        if (*s == '\0') {
            free(buf);
            return CURSOR_BAD;
        }
        if (*s == '"') {
            s++;
            break;
        }
        if (*s == '\\') {
            s++;
            st = json_escape(&s, &c);
            if (st != CURSOR_OK) {
                free(buf);
                return st;
            }
        } else if ((unsigned char)*s < ' ') {
            free(buf);
            return CURSOR_BAD;
        } else {
            c = (unsigned char)*s;
            s++;
        }
        st = buf_add(&buf, &n, &cap, (char)c);
        if (st != CURSOR_OK) {
            free(buf);
            return st;
        }
    }
    if (buf == NULL) {
        buf = (char *)malloc(1U);
        if (buf == NULL) {
            return -1;
        }
        buf[0] = '\0';
    }
    *out = buf;
    *p = s;
    return CURSOR_OK;
}

static int parse_json_uint(const char **p, long long *out)
{
    const char *s = *p;
    unsigned long long value = 0ULL;

    if (*s < '0' || *s > '9') {
        return CURSOR_BAD;
    }
    if (*s == '0' && s[1] >= '0' && s[1] <= '9') {
        return CURSOR_BAD;
    }
    while (*s >= '0' && *s <= '9') {
        unsigned digit = (unsigned)(*s - '0');
        if (value > ((unsigned long long)LLONG_MAX - digit) / (unsigned long long)DECIMAL_BASE) {
            return CURSOR_BAD;
        }
        value = (value * (unsigned long long)DECIMAL_BASE) + digit;
        s++;
    }
    *out = (long long)value;
    *p = s;
    return CURSOR_OK;
}

static bool starts_with_key(const char *p, const char *key)
{
    size_t n = strlen(key);

    if (p[0] == ',' && p[1] == '"' && strncmp(p + 2, key, n) == 0 && p[2U + n] == '"') {
        return true;
    }
    return false;
}

static int take_key(const char **p, const char *key, bool comma)
{
    char *got = NULL;
    int st = CURSOR_OK;

    if (comma) {
        if (**p != ',') {
            return CURSOR_BAD;
        }
        (*p)++;
    }
    st = parse_json_string(p, &got);
    if (st != CURSOR_OK) {
        return st;
    }
    if (strcmp(got, key) != 0) {
        free(got);
        return CURSOR_BAD;
    }
    free(got);
    if (**p != ':') {
        return CURSOR_BAD;
    }
    (*p)++;
    return CURSOR_OK;
}

static int take_req_string(const char **p, const char *key, bool comma, char **slot)
{
    int st = take_key(p, key, comma);

    if (st != CURSOR_OK) {
        return st;
    }
    st = parse_json_string(p, slot);
    if (st != CURSOR_OK) {
        return st;
    }
    if ((*slot)[0] == '\0') {
        return CURSOR_BAD;
    }
    return CURSOR_OK;
}

static int take_opt_string(const char **p, const char *key, char **slot)
{
    int st = CURSOR_OK;

    if (!starts_with_key(*p, key)) {
        return CURSOR_OK;
    }
    st = take_req_string(p, key, true, slot);
    return st;
}

static int take_tags(const char **p, CursorDoc *doc)
{
    int st = CURSOR_OK;

    if (!starts_with_key(*p, "tags")) {
        return CURSOR_OK;
    }
    st = take_key(p, "tags", true);
    if (st != CURSOR_OK) {
        return st;
    }
    if (**p != '[') {
        return CURSOR_BAD;
    }
    (*p)++;
    if (**p == ']') {
        return CURSOR_BAD;
    }
    for (;;) {
        char *tag = NULL;

        if (doc->ntags >= (size_t)CURSOR_TAG_MAX) {
            return CURSOR_BAD;
        }
        st = parse_json_string(p, &tag);
        if (st != CURSOR_OK) {
            return st;
        }
        if (tag[0] == '\0' || (doc->ntags > 0U && strcmp(doc->tags[doc->ntags - 1U], tag) > 0)) {
            free(tag);
            return CURSOR_BAD;
        }
        doc->tags[doc->ntags++] = tag;
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == ']') {
            (*p)++;
            return CURSOR_OK;
        }
        return CURSOR_BAD;
    }
}

static bool sort_token_ok(const char *text)
{
    if (strcmp(text, "updated") == 0 || strcmp(text, "created") == 0) {
        return true;
    }
    return false;
}

static bool bin_token_ok(const char *text)
{
    if (strcmp(text, "live") == 0 || strcmp(text, "expired") == 0 || strcmp(text, "deleted") == 0) {
        return true;
    }
    return false;
}

static int cursor_decode(const char *raw, CursorDoc *doc)
{
    unsigned char *json = NULL;
    size_t n = 0U;
    const char *p = NULL;
    int st = CURSOR_OK;
    long long version = 0;

    memset(doc, 0, sizeof(*doc));
    st = base64url_decode(raw, &json, &n);
    if (st != CURSOR_OK) {
        return st;
    }
    p = (const char *)json;
    if (*p != '{') {
        free(json);
        return CURSOR_BAD;
    }
    p++;
    st = take_key(&p, "v", false);
    if (st == CURSOR_OK) {
        st = parse_json_uint(&p, &version);
    }
    if (st == CURSOR_OK && version != 1) {
        st = CURSOR_BAD;
    }
    if (st == CURSOR_OK) {
        st = take_req_string(&p, "sort", true, &doc->sort);
    }
    if (st == CURSOR_OK && !sort_token_ok(doc->sort)) {
        st = CURSOR_BAD;
    }
    if (st == CURSOR_OK) {
        st = take_req_string(&p, "bin", true, &doc->bin);
    }
    if (st == CURSOR_OK && !bin_token_ok(doc->bin)) {
        st = CURSOR_BAD;
    }
    if (st == CURSOR_OK) {
        st = take_tags(&p, doc);
    }
    if (st == CURSOR_OK) {
        st = take_opt_string(&p, "created_by", &doc->created_by);
    }
    if (st == CURSOR_OK) {
        st = take_opt_string(&p, "updated_by", &doc->updated_by);
    }
    if (st == CURSOR_OK) {
        st = take_opt_string(&p, "key", &doc->key);
    }
    if (st == CURSOR_OK) {
        st = take_opt_string(&p, "since", &doc->since);
    }
    if (st == CURSOR_OK) {
        st = take_opt_string(&p, "until", &doc->until);
    }
    if (st == CURSOR_OK) {
        st = take_req_string(&p, "at", true, &doc->at);
    }
    if (st == CURSOR_OK) {
        st = take_key(&p, "local_id", true);
    }
    if (st == CURSOR_OK) {
        st = parse_json_uint(&p, &doc->local_id);
    }
    if (st == CURSOR_OK && (*p != '}' || p[1] != '\0')) {
        st = CURSOR_BAD;
    }
    free(json);
    return st;
}

static const char *sort_token(StoreSort sort)
{
    return sort == STORE_SORT_CREATED ? "created" : "updated";
}

static const char *bin_token(StoreBin bin)
{
    switch (bin) {
    case STORE_BIN_LIVE:
        return "live";
    case STORE_BIN_EXPIRED:
        return "expired";
    case STORE_BIN_DELETED:
        return "deleted";
    default:
        return NULL;
    }
}

/* qsort passes the two element slots. The caller cannot swap the parameters. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int cmp_cstr(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;

    return strcmp(*a, *b);
}

static int write_opt_string(FILE *fp, const char *key, const char *value)
{
    if (value == NULL) {
        return 0;
    }
    if (fprintf(fp, ",\"%s\":", key) < 0 || output_json_string(fp, value) != 0) {
        return -1;
    }
    return 0;
}

static int write_cursor_tags(FILE *fp, const char *const *tags, size_t ntags)
{
    size_t i = 0U;

    if (ntags == 0U) {
        return 0;
    }
    if (fputs(",\"tags\":[", fp) < 0) {
        return -1;
    }
    for (i = 0U; i < ntags; i++) {
        if ((i > 0U && fputc(',', fp) == EOF) || output_json_string(fp, tags[i]) != 0) {
            return -1;
        }
    }
    if (fputc(']', fp) == EOF) {
        return -1;
    }
    return 0;
}

static char *list_cursor_encode(const ListQuery *q, const char *at, long long local_id)
{
    char *json = NULL;
    size_t len = 0U;
    FILE *fp = open_memstream(&json, &len);
    char *encoded = NULL;
    const char **tags = NULL;
    const char *sort = sort_token(q->sort);
    const char *bin = bin_token(q->bin);
    size_t i = 0U;

    if (fp == NULL) {
        return NULL;
    }
    if (at == NULL || bin == NULL) {
        goto fail;
    }
    if (q->ntags > 0U) {
        tags = (const char **)malloc(q->ntags * sizeof(*tags));
        if (tags == NULL) {
            goto fail;
        }
        for (i = 0U; i < q->ntags; i++) {
            tags[i] = q->tags[i];
        }
        qsort((void *)tags, q->ntags, sizeof(*tags), cmp_cstr);
    }
    if (fputs("{\"v\":1,\"sort\":", fp) < 0 || output_json_string(fp, sort) != 0 ||
        fputs(",\"bin\":", fp) < 0 || output_json_string(fp, bin) != 0) {
        goto fail;
    }
    if (write_cursor_tags(fp, tags, q->ntags) != 0) {
        goto fail;
    }
    if (write_opt_string(fp, "created_by", q->created_by) != 0 ||
        write_opt_string(fp, "updated_by", q->updated_by) != 0 ||
        write_opt_string(fp, "key", q->key) != 0 || write_opt_string(fp, "since", q->since) != 0 ||
        write_opt_string(fp, "until", q->until) != 0 || fputs(",\"at\":", fp) < 0 ||
        output_json_string(fp, at) != 0 || fprintf(fp, ",\"local_id\":%lld}", local_id) < 0) {
        goto fail;
    }
    if (fclose(fp) != 0) {
        free(json);
        free((void *)tags);
        return NULL;
    }
    encoded = base64url_encode((const unsigned char *)json, len);
    free(json);
    free((void *)tags);
    return encoded;

fail:
    (void)fclose(fp);
    free(json);
    free((void *)tags);
    return NULL;
}

/* A filter, sort or bin flag next to --cursor (the cursor carries the query). */
static bool list_has_query_flags(const ListParse *p)
{
    if (p->created_by != NULL || p->updated_by != NULL || p->key_raw != NULL ||
        p->sort_raw != NULL || p->since_raw != NULL || p->until_raw != NULL || p->ntag_raw > 0U ||
        p->bins.expired || p->bins.deleted || p->bins.trash) {
        return true;
    }
    return false;
}

/* The cursor's sort value is a stored timestamp: parsing it gives the same bytes.
   since and until need no such check: the re-encode in list_query_from_cursor
   rejects a bound that is not canonical. */
static bool cursor_at_ok(const char *at)
{
    char canonical[ISO_TS_BUFSIZE];

    if (parse_filter_ts(at, canonical) != 0 || strcmp(canonical, at) != 0) {
        return false;
    }
    return true;
}

/* The query a decoded cursor names, checked the way the flags are, without
   printing. key_norm, since and until are the caller's buffers. */
static int cursor_fill_query(const CursorDoc *doc, ListQuery *q, char *key_norm, size_t key_norm_sz,
                             char ***out_tags, size_t *out_ntags, char since[ISO_TS_BUFSIZE],
                             char until[ISO_TS_BUFSIZE])
{
    const char *err = NULL;

    q->sort = (strcmp(doc->sort, "created") == 0) ? STORE_SORT_CREATED : STORE_SORT_UPDATED;
    q->bin = STORE_BIN_LIVE;
    if (strcmp(doc->bin, "expired") == 0) {
        q->bin = STORE_BIN_EXPIRED;
    } else if (strcmp(doc->bin, "deleted") == 0) {
        q->bin = STORE_BIN_DELETED;
    }
    if ((doc->created_by != NULL && !actor_kind_is_stored(doc->created_by)) ||
        (doc->updated_by != NULL && !actor_kind_is_stored(doc->updated_by)) ||
        !cursor_at_ok(doc->at) || (doc->since != NULL && parse_filter_ts(doc->since, since) != 0) ||
        (doc->until != NULL && parse_filter_ts(doc->until, until) != 0)) {
        return CURSOR_BAD;
    }
    if (doc->key != NULL) {
        if (normalize_key(doc->key, key_norm, key_norm_sz) != NORM_OK) {
            return CURSOR_BAD;
        }
        q->key = key_norm;
    }
    if (normalize_tags((const char *const *)doc->tags, doc->ntags, out_tags, out_ntags, &err) !=
        0) {
        return CURSOR_BAD;
    }
    q->tags = (const char *const *)*out_tags;
    q->ntags = *out_ntags;
    q->created_by = doc->created_by;
    q->updated_by = doc->updated_by;
    q->since = (doc->since != NULL) ? since : NULL;
    q->until = (doc->until != NULL) ? until : NULL;
    q->after_at = doc->at;
    q->after_local_id = doc->local_id;
    return CURSOR_OK;
}

/* --cursor alone continues the query it names (decision:plan21-cursor-binds-whole-query).
   Only --limit may come with it. The cursor must be the one this binary writes
   for that query, byte for byte, so a normalized or reordered copy is rejected.
   doc owns the strings q points at. */
static int list_query_from_cursor(const ListParse *parsed, CursorDoc *doc, ListQuery *q,
                                  char *key_norm, size_t key_norm_sz, char ***out_tags,
                                  size_t *out_ntags, char since[ISO_TS_BUFSIZE],
                                  char until[ISO_TS_BUFSIZE])
{
    const char *raw = parsed->cursor;
    StoreBin flagged = STORE_BIN_LIVE;
    int st = CURSOR_OK;

    /* The resolver still warns on --trash and rejects a bad bin pair. */
    if (cmd_bin_resolve(&parsed->bins, &flagged) != 0) {
        return -1;
    }
    if (list_has_query_flags(parsed)) {
        err_msg(DIAG_INVALID_ARGUMENT, "invalid --cursor");
        return -1;
    }
    q->limit = parsed->limit;
    st = cursor_decode(raw, doc);

    if (st == CURSOR_OK) {
        st = cursor_fill_query(doc, q, key_norm, key_norm_sz, out_tags, out_ntags, since, until);
    }
    if (st == CURSOR_OK) {
        char *again = list_cursor_encode(q, doc->at, doc->local_id);

        if (again == NULL) {
            err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
            return -1;
        }
        if (strcmp(again, raw) != 0) {
            st = CURSOR_BAD;
        }
        free(again);
    }
    if (st == CURSOR_OOM) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return -1;
    }
    if (st != CURSOR_OK) {
        err_msg(DIAG_INVALID_ARGUMENT, "invalid --cursor");
        return -1;
    }
    return 0;
}

static int list_publish_page(const ListQuery *q, const Entry *entries, size_t count, bool has_more)
{
    const char *at = NULL;
    char *cursor = NULL;
    int rc = 0;

    if (!has_more) {
        return app_set_list_page(NULL);
    }
    if (count == 0U) {
        err_msg(DIAG_INTERNAL, "internal error");
        return -1;
    }
    at = (q->sort == STORE_SORT_CREATED) ? entries[count - 1U].created_at
                                         : entries[count - 1U].updated_at;
    if (at == NULL) {
        err_msg(DIAG_INTERNAL, "internal error");
        return -1;
    }
    cursor = list_cursor_encode(q, at, entries[count - 1U].id);
    if (cursor == NULL) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return -1;
    }
    rc = app_set_list_page(cursor);
    free(cursor);
    if (rc != 0) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
    }
    return rc;
}

int cmd_list(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    ListParse parsed;
    const char *err = NULL;
    char **tags_norm = NULL;
    size_t ntags = 0U;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char since[ISO_TS_BUFSIZE];
    char until[ISO_TS_BUFSIZE];
    ListQuery q;
    Entry *entries = NULL;
    size_t count = 0U;
    size_t total = 0U;
    StoreNeighbor *links = NULL;
    size_t nlinks = 0U;
    StoreStatus st = STORE_OK;
    int rc = REMEMBER_ERR;
    char now[ISO_TS_BUFSIZE];
    CursorDoc doc;
    bool has_more = false;

    memset(&q, 0, sizeof(q));
    memset(&doc, 0, sizeof(doc));
    memset(key_norm, 0, sizeof(key_norm));

    if (parse_list_args(rest_argc, rest_argv, &parsed, &err) != 0) {
        if (err != NULL && err[0] != '\0') {
            err_msg(code_for_cmd_text(err), err);
        }
        list_parse_free(&parsed);
        return REMEMBER_ERR;
    }

    if (parsed.cursor == NULL) {
        if (list_prepare_query(&parsed, key_norm, sizeof(key_norm), &tags_norm, &ntags, &q, since,
                               until) != 0) {
            goto cleanup;
        }
    } else if (list_query_from_cursor(&parsed, &doc, &q, key_norm, sizeof(key_norm), &tags_norm,
                                      &ntags, since, until) != 0) {
        goto cleanup;
    }

    if (utc_now(now, sizeof(now)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        goto cleanup;
    }
    {
        PageResult page = store_list(s, &q, now);
        st = page.st;
        entries = page.entries;
        count = page.count;
        total = page.total;
        has_more = page.has_more;
    }
    if (st != STORE_OK) {
        err_store(s, st);
        goto cleanup;
    }
    if (load_page_neighbors(s, entries, count, now, &links, &nlinks) != 0) {
        goto cleanup;
    }
    if (q.bin != STORE_BIN_DELETED) {
        cmd_neighbors_drop_deleted(links, &nlinks, now);
    }

    if (emit_entry_page(json, count, total, entries, links, nlinks, now) != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        goto cleanup;
    }
    if (json && list_publish_page(&q, entries, count, has_more) != 0) {
        goto cleanup;
    }
    rc = REMEMBER_OK;

cleanup:
    list_parse_free(&parsed);
    cursor_doc_free(&doc);
    free_tag_list(tags_norm, ntags);
    store_neighbors_free(links, nlinks);
    free_entry_page(entries, count);
    return rc;
}

int cmd_search(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    SearchParse parsed;
    const char *err = NULL;
    char **tags_norm = NULL;
    size_t ntags = 0U;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char since[ISO_TS_BUFSIZE];
    char until[ISO_TS_BUFSIZE];
    SearchQuery q;
    SearchResult res = {.page = {.st = STORE_OK}};
    StoreNeighbor *links = NULL;
    size_t nlinks = 0U;
    int rc = REMEMBER_ERR;
    char now[ISO_TS_BUFSIZE];

    memset(&q, 0, sizeof(q));
    memset(key_norm, 0, sizeof(key_norm));

    if (parse_search_args(rest_argc, rest_argv, &parsed, &err) != 0) {
        if (err != NULL && err[0] != '\0') {
            err_msg(code_for_cmd_text(err), err);
        }
        list_parse_free(&parsed.filters);
        return REMEMBER_ERR;
    }

    if (list_prepare_query(&parsed.filters, key_norm, sizeof(key_norm), &tags_norm, &ntags,
                           &q.filters, since, until) != 0) {
        goto cleanup;
    }
    q.query = parsed.query;
    q.facet_limit = parsed.facet_limit;

    if (utc_now(now, sizeof(now)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        goto cleanup;
    }
    res = store_search(s, &q, now);
    if (res.page.st != STORE_OK) {
        err_store(s, res.page.st);
        goto cleanup;
    }
    if (load_page_neighbors(s, res.page.entries, res.page.count, now, &links, &nlinks) != 0) {
        goto cleanup;
    }
    if (q.filters.bin != STORE_BIN_DELETED) {
        cmd_neighbors_drop_deleted(links, &nlinks, now);
    }

    if (emit_search(json, &q, &res, links, nlinks, now) != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        goto cleanup;
    }
    if (res.warning != NULL) {
        (void)diag_warning(DIAG_WARN_RAW_QUERY_IGNORED, res.warning);
    }
    rc = REMEMBER_OK;

cleanup:
    list_parse_free(&parsed.filters);
    free_tag_list(tags_norm, ntags);
    store_neighbors_free(links, nlinks);
    store_search_result_free(&res);
    return rc;
}
