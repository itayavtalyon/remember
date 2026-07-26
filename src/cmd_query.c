#include "commands.h"
#include "commands_common.h"

#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "store.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* list / search: shared filter + paging parse, different store call + query. */

#define LIST_LIMIT_DEFAULT 20U
#define LIST_LIMIT_MAX 1000U
/* Bounded well under the store's per-query parameter budget (LIST_BIND_CAP) so
   an over-cap filter set is a clear user error here, not an opaque store one. */
#define LIST_TAG_FILTER_MAX 50U

/* Parse a non-negative size_t from decimal text. Rejects empty, non-digits, ERANGE. */
static int parse_size_token(const char *raw, size_t *out)
{
    char *end = NULL;
    unsigned long long v;

    if (raw == NULL || raw[0] == '\0' || raw[0] == '-') {
        return -1;
    }
    errno = 0;
    v = strtoull(raw, &end, 10);
    if (end == raw || (end != NULL && *end != '\0') || errno == ERANGE) {
        return -1;
    }
#if SIZE_MAX < ULLONG_MAX
    /* 32-bit size_t: reject rather than silently truncate the value. */
    if (v > (unsigned long long)SIZE_MAX) {
        return -1;
    }
#endif
    *out = (size_t)v;
    return 0;
}

typedef struct {
    const char *source;
    const char *key_raw;
    const char **tag_raw;
    size_t ntag_raw;
    size_t limit;
    size_t offset;
} ListParse;

static void list_parse_free(ListParse *p)
{
    free((void *)p->tag_raw);
    p->tag_raw = NULL;
    p->ntag_raw = 0U;
}

static int list_take_limit(int *i, int rest_argc, const char **rest_argv, size_t *out_limit,
                           const char **err)
{
    const char *val = NULL;
    size_t lim = 0U;

    if (take_value(i, rest_argc, rest_argv, &val, err, "missing value for --limit") != 0) {
        return -1;
    }
    if (parse_size_token(val, &lim) != 0 || lim == 0U || lim > LIST_LIMIT_MAX) {
        *err = "invalid --limit (must be 1..1000)";
        return -1;
    }
    *out_limit = lim;
    return 0;
}

static int list_take_offset(int *i, int rest_argc, const char **rest_argv, size_t *out_offset,
                            const char **err)
{
    const char *val = NULL;
    size_t off = 0U;

    if (take_value(i, rest_argc, rest_argv, &val, err, "missing value for --offset") != 0) {
        return -1;
    }
    if (parse_size_token(val, &off) != 0) {
        *err = "invalid --offset (must be >= 0)";
        return -1;
    }
    *out_offset = off;
    return 0;
}

/*
 * Handle one list/search option. Returns: 0 handled, 1 end-opts, 2 positional
 * for caller, -1 error (*err set; empty string if message already printed).
 */
static int list_handle_opt(const char *arg, int *i, int rest_argc, const char **rest_argv,
                           ListParse *out, size_t *tag_cap, const char **err)
{
    const char *val = NULL;

    if (strcmp(arg, "--") == 0) {
        return 1;
    }
    if (strcmp(arg, "--tag") == 0) {
        if (take_value(i, rest_argc, rest_argv, &val, err, "missing value for --tag") != 0) {
            return -1;
        }
        if (push_cstr_ptr(&out->tag_raw, &out->ntag_raw, tag_cap, val) != 0) {
            *err = "out of memory";
            return -1;
        }
        return 0;
    }
    if (strcmp(arg, "--source") == 0) {
        return take_value(i, rest_argc, rest_argv, &out->source, err, "missing value for --source");
    }
    if (strcmp(arg, "--key") == 0) {
        return take_value(i, rest_argc, rest_argv, &out->key_raw, err, "missing value for --key");
    }
    if (strcmp(arg, "--limit") == 0) {
        return list_take_limit(i, rest_argc, rest_argv, &out->limit, err);
    }
    if (strcmp(arg, "--offset") == 0) {
        return list_take_offset(i, rest_argc, rest_argv, &out->offset, err);
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        (void)fprintf(stderr, "remember: unknown option '%s'\n", arg);
        *err = "";
        return -1;
    }
    return 2;
}

static void list_parse_init(ListParse *out)
{
    out->source = NULL;
    out->key_raw = NULL;
    out->tag_raw = NULL;
    out->ntag_raw = 0U;
    out->limit = LIST_LIMIT_DEFAULT;
    out->offset = 0U;
}

static int parse_list_args(int rest_argc, const char **rest_argv, ListParse *out, const char **err)
{
    int i;
    int end_opts = 0;
    size_t tag_cap = 0U;

    list_parse_init(out);
    *err = NULL;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];
        int kind;

        if (end_opts) {
            *err = "unexpected argument";
            return -1;
        }
        kind = list_handle_opt(arg, &i, rest_argc, rest_argv, out, &tag_cap, err);
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
 * Same filters/paging as list, plus exactly one required QUERY positional
 * (FTS5 MATCH string; outer ASCII whitespace trimmed — same set as normalize).
 */
typedef struct {
    ListParse filters;
    const char *query_raw; /* argv token during parse; not owned */
    char *query;           /* heap-owned trimmed MATCH string */
} SearchParse;

static void search_parse_free(SearchParse *p)
{
    list_parse_free(&p->filters);
    free(p->query);
    p->query = NULL;
    p->query_raw = NULL;
}

/* intentional: same ASCII ws as normalize.c (space/tab/LF/CR/VT/FF). */
static int is_ascii_ws(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/*
 * Heap-copy of raw with leading/trailing ASCII whitespace removed.
 * NULL + *err on empty-after-trim or OOM.
 */
static char *trim_query_copy(const char *raw, const char **err)
{
    size_t len;
    size_t start = 0U;
    size_t end;
    size_t n;
    char *out;

    if (raw == NULL) {
        *err = "empty search query";
        return NULL;
    }
    len = strlen(raw);
    end = len;
    while (start < end && is_ascii_ws((unsigned char)raw[start])) {
        start++;
    }
    while (end > start && is_ascii_ws((unsigned char)raw[end - 1U])) {
        end--;
    }
    if (start == end) {
        *err = "empty search query";
        return NULL;
    }
    n = end - start;
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        *err = "out of memory";
        return NULL;
    }
    memcpy(out, raw + start, n);
    out[n] = '\0';
    return out;
}

/* Accept the single positional QUERY; a second positional is a user error. */
static int search_set_query(SearchParse *out, const char *arg, const char **err)
{
    if (out->query_raw != NULL) {
        *err = "unexpected argument";
        return -1;
    }
    out->query_raw = arg;
    return 0;
}

static int parse_search_args(int rest_argc, const char **rest_argv, SearchParse *out,
                             const char **err)
{
    int i;
    int end_opts = 0;
    size_t tag_cap = 0U;

    list_parse_init(&out->filters);
    out->query_raw = NULL;
    out->query = NULL;
    *err = NULL;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];
        int kind;

        if (end_opts) {
            if (search_set_query(out, arg, err) != 0) {
                return -1;
            }
            continue;
        }
        kind = list_handle_opt(arg, &i, rest_argc, rest_argv, &out->filters, &tag_cap, err);
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
        /* Positional: the FTS query (exactly one). */
        if (search_set_query(out, arg, err) != 0) {
            return -1;
        }
    }
    if (out->query_raw == NULL) {
        *err = "missing search query";
        return -1;
    }
    out->query = trim_query_copy(out->query_raw, err);
    if (out->query == NULL) {
        return -1;
    }
    return 0;
}

/*
 * Validate parsed filters and normalize them into *q. Allocates *out_tags (heap;
 * caller frees with free_tag_list) that q->tags points into; key_norm is caller
 * storage that q->key points into. Prints its own error and returns -1 on
 * failure, 0 on success.
 */
static int list_prepare_query(const ListParse *parsed, char *key_norm, size_t key_norm_sz,
                              char ***out_tags, size_t *out_ntags, ListQuery *q)
{
    const char *err = NULL;

    *out_tags = NULL;
    *out_ntags = 0U;
    q->key = NULL; /* default; set below only when --key was given */

    if (parsed->source != NULL && !source_is_valid(parsed->source)) {
        err_msg("invalid source (use human, agent, tool, or unknown)");
        return -1;
    }
    if (parsed->ntag_raw > LIST_TAG_FILTER_MAX) {
        err_msg("too many --tag filters (max 50)");
        return -1;
    }
    if (normalize_tags((const char *const *)parsed->tag_raw, parsed->ntag_raw, out_tags, out_ntags,
                       &err) != 0) {
        err_msg(err);
        return -1;
    }
    if (parsed->key_raw != NULL) {
        NormStatus ns = normalize_key(parsed->key_raw, key_norm, key_norm_sz);
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return -1;
        }
        q->key = key_norm;
    }
    q->tags = (const char *const *)*out_tags;
    q->ntags = *out_ntags;
    q->source = parsed->source;
    q->limit = parsed->limit;
    q->offset = parsed->offset;
    return 0;
}

/* Emit list/search page: JSON envelope or human preview lines. */
static int emit_entry_page(bool json, size_t offset, size_t limit, size_t count, size_t total,
                           const Entry *entries)
{
    size_t i;

    if (json) {
        return output_list_envelope(stdout, offset, limit, count, total, entries);
    }
    for (i = 0; i < count; i++) {
        if (output_entry_human_line(stdout, &entries[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static void free_entry_page(Entry *entries, size_t count)
{
    size_t i;

    if (entries == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        store_entry_free(&entries[i]);
    }
    free(entries);
}

int cmd_list(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    ListParse parsed;
    const char *err = NULL;
    char **tags_norm = NULL;
    size_t ntags = 0U;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    ListQuery q;
    Entry *entries = NULL;
    size_t count = 0U;
    size_t total = 0U;
    StoreStatus st;
    int rc = REMEMBER_ERR;

    memset(&q, 0, sizeof(q));
    memset(key_norm, 0, sizeof(key_norm));

    if (parse_list_args(rest_argc, rest_argv, &parsed, &err) != 0) {
        if (err != NULL && err[0] != '\0') {
            err_msg(err);
        }
        list_parse_free(&parsed);
        return REMEMBER_ERR;
    }

    if (list_prepare_query(&parsed, key_norm, sizeof(key_norm), &tags_norm, &ntags, &q) != 0) {
        goto cleanup;
    }

    st = store_list(s, &q, &entries, &count, &total);
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        goto cleanup;
    }

    if (emit_entry_page(json, q.offset, q.limit, count, total, entries) != 0) {
        err_msg("failed to write output");
        goto cleanup;
    }
    rc = REMEMBER_OK;

cleanup:
    list_parse_free(&parsed);
    free_tag_list(tags_norm, ntags);
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
    SearchQuery q;
    Entry *entries = NULL;
    size_t count = 0U;
    size_t total = 0U;
    StoreStatus st;
    int rc = REMEMBER_ERR;

    memset(&q, 0, sizeof(q));
    memset(key_norm, 0, sizeof(key_norm));

    if (parse_search_args(rest_argc, rest_argv, &parsed, &err) != 0) {
        if (err != NULL && err[0] != '\0') {
            err_msg(err);
        }
        search_parse_free(&parsed);
        return REMEMBER_ERR;
    }

    if (list_prepare_query(&parsed.filters, key_norm, sizeof(key_norm), &tags_norm, &ntags,
                           &q.filters) != 0) {
        goto cleanup;
    }
    q.query = parsed.query;

    st = store_search(s, &q, &entries, &count, &total);
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        goto cleanup;
    }

    if (emit_entry_page(json, q.filters.offset, q.filters.limit, count, total, entries) != 0) {
        err_msg("failed to write output");
        goto cleanup;
    }
    rc = REMEMBER_OK;

cleanup:
    search_parse_free(&parsed);
    free_tag_list(tags_norm, ntags);
    free_entry_page(entries, count);
    return rc;
}
