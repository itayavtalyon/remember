#include "output.h"

#include "normalize.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>

/* ASCII / UTF-8 byte constants (see also normalize.c). Unsigned to keep the
   mask arithmetic unsigned. */
static const unsigned ASCII_FIRST_PRINTABLE = 0x20U; /* first non-control byte */
static const unsigned ASCII_DEL = 0x7FU;             /* DEL control */
static const unsigned UTF8_C1_LEAD = 0xC2U;          /* lead byte of U+0080..U+00BF */
static const unsigned UTF8_C1_FIRST = 0x80U;         /* U+0080 continuation byte */
static const unsigned UTF8_C1_LAST = 0x9FU;          /* U+009F continuation byte */
static const unsigned UTF8_HIGH_BIT = 0x80U;         /* set => multibyte lead/continuation */
static const unsigned UTF8_LEAD2_MASK = 0xE0U;
static const unsigned UTF8_LEAD2_TAG = 0xC0U;
static const unsigned UTF8_LEAD3_MASK = 0xF0U;
static const unsigned UTF8_LEAD3_TAG = 0xE0U;
static const unsigned UTF8_LEAD4_MASK = 0xF8U;
static const unsigned UTF8_LEAD4_TAG = 0xF0U;

static const char *empty_str(void)
{
    static const char e[] = "";
    return e;
}

static int write_json_escape(FILE *out, unsigned char c)
{
    switch (c) {
    case '"':
        return fputs("\\\"", out);
    case '\\':
        return fputs("\\\\", out);
    case '\b':
        return fputs("\\b", out);
    case '\f':
        return fputs("\\f", out);
    case '\n':
        return fputs("\\n", out);
    case '\r':
        return fputs("\\r", out);
    case '\t':
        return fputs("\\t", out);
    default:
        if (c < ASCII_FIRST_PRINTABLE) {
            return fprintf(out, "\\u%04x", (unsigned)c);
        }
        return fputc((int)c, out);
    }
}

/* cppcheck-suppress staticFunction ; public API (output.h); used by tests */
int output_json_string(FILE *out, const char *s)
{
    const unsigned char *p = NULL;

    if (out == NULL) {
        return -1;
    }
    if (s == NULL) {
        s = empty_str();
    }
    if (fputc('"', out) == EOF) {
        return -1;
    }
    for (p = (const unsigned char *)s; *p != '\0'; p++) {
        if (write_json_escape(out, *p) < 0) {
            return -1;
        }
    }
    if (fputc('"', out) == EOF) {
        return -1;
    }
    return 0;
}

/* One JSON object field: its name and string value (NULL renders as ""). */
typedef struct {
    const char *name;
    const char *value;
} JsonField;

static int write_json_field_str(FILE *out, JsonField field)
{
    if (fprintf(out, ",\"%s\":", field.name) < 0) {
        return -1;
    }
    return output_json_string(out, field.value);
}

static int write_json_nullable(FILE *out, const char *name, const char *value)
{
    if (value == NULL) {
        return fprintf(out, ",\"%s\":null", name) < 0 ? -1 : 0;
    }
    return write_json_field_str(out, (JsonField){.name = name, .value = value});
}

/* Indexed by a store_bin_of result, which is always one of the three bins. */
static const char *const BIN_TOKENS[] = {
    [STORE_BIN_LIVE] = "live",
    [STORE_BIN_EXPIRED] = "expired",
    [STORE_BIN_DELETED] = "deleted",
};

static int write_json_tags(FILE *out, char *const *tags, size_t ntags)
{
    size_t i = 0;

    if (fputs(",\"tags\":[", out) < 0) {
        return -1;
    }
    for (i = 0; i < ntags; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_json_string(out, tags[i]) != 0) {
            return -1;
        }
    }
    return fputs("]", out) < 0 ? -1 : 0;
}

static int write_json_raw_object(FILE *out, JsonField field)
{
    if (fprintf(out, ",\"%s\":", field.name) < 0) {
        return -1;
    }
    return fputs(field.value, out) < 0 ? -1 : 0;
}

static int write_entry_core(FILE *out, const Entry *e, const char *now)
{
    const char *bin = NULL;

    /* The vector is written raw, so only the canonical compact form may pass
       (decision:plan18c-validate-peer-fields). */
    if (now == NULL || e->sync_id == NULL || !vv_json_is_canonical(e->version_vector)) {
        return -1;
    }
    bin = BIN_TOKENS[store_bin_of(e->deleted_at, e->expires_at, now)];
    if (fprintf(out, "{\"id\":%lld,\"sync_id\":", e->id) < 0) {
        return -1;
    }
    if (output_json_string(out, e->sync_id) != 0) {
        return -1;
    }
    if (write_json_nullable(out, "key", e->key) != 0) {
        return -1;
    }
    if (write_json_field_str(out, (JsonField){.name = "body", .value = e->body}) != 0) {
        return -1;
    }
    if (write_json_tags(out, e->tags, e->ntags) != 0) {
        return -1;
    }
    if (write_json_field_str(
            out, (JsonField){.name = "source",
                             .value = e->source != NULL ? e->source : "unknown"}) != 0) {
        return -1;
    }
    if (write_json_field_str(out, (JsonField){.name = "created_at", .value = e->created_at}) != 0) {
        return -1;
    }
    if (write_json_field_str(out, (JsonField){.name = "updated_at", .value = e->updated_at}) != 0) {
        return -1;
    }
    if (write_json_nullable(out, "expires_at", e->expires_at) != 0) {
        return -1;
    }
    if (write_json_nullable(out, "deleted_at", e->deleted_at) != 0) {
        return -1;
    }
    if (write_json_field_str(out, (JsonField){.name = "bin", .value = bin}) != 0) {
        return -1;
    }
    return write_json_raw_object(out,
                                 (JsonField){.name = "version_vector", .value = e->version_vector});
}

/* cppcheck-suppress staticFunction ; public API (output.h); used by tests */
int output_entry_json(FILE *out, const Entry *e, const char *now)
{
    if (out == NULL || e == NULL) {
        return -1;
    }
    if (write_entry_core(out, e, now) != 0) {
        return -1;
    }
    if (fputc('}', out) == EOF) {
        return -1;
    }
    return 0;
}

static int write_entry_with_links(FILE *out, const Entry *e, const StoreNeighbor *links,
                                  size_t nlinks, const char *now);

int output_action_envelope(FILE *out, const char *action, const Entry *e, const char *now)
{
    if (out == NULL || action == NULL || e == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"action\":") < 0) {
        return -1;
    }
    if (output_json_string(out, action) != 0) {
        return -1;
    }
    if (fputs(",\"count\":1,\"entries\":[", out) < 0) {
        return -1;
    }
    if (output_entry_json(out, e, now) != 0) {
        return -1;
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_deleted_list(FILE *out, const Entry *entries, size_t count, const char *now)
{
    size_t i = 0;

    if (out == NULL) {
        return -1;
    }
    if (count > 0U && entries == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"action\":\"deleted\",\"count\":%zu,\"entries\":[", count) <
        0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_entry_json(out, &entries[i], now) != 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_get_envelope(FILE *out, const Entry *e, const StoreNeighbor *links, size_t nlinks,
                        const char *now)
{
    if (out == NULL || e == NULL || now == NULL) {
        return -1;
    }
    if (nlinks > 0U && links == NULL) {
        return -1;
    }
    if (fputs("{\"version\":1,\"count\":1,\"entries\":[", out) < 0) {
        return -1;
    }
    if (write_entry_with_links(out, e, links, nlinks, now) != 0) {
        return -1;
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

/* Shared head of the list and search envelopes, through "total". */
static int write_page_head(FILE *out, size_t offset, size_t limit, size_t count, size_t total)
{
    return fprintf(out, "{\"version\":1,\"offset\":%zu,\"limit\":%zu,\"count\":%zu,\"total\":%zu",
                   offset, limit, count, total) < 0
               ? -1
               : 0;
}

/* Shared tail: the entries array, then the closing brace and newline. */
static int write_page_entries(FILE *out, const Entry *entries, size_t count,
                              const StoreNeighbor *links, size_t nlinks, const char *now)
{
    size_t i = 0;

    if (fputs(",\"entries\":[", out) < 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (write_entry_with_links(out, &entries[i], links, nlinks, now) != 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

/* The arguments both page envelopes share: a stream, now, and arrays that
   hold what their counts say. */
static bool page_args_ok(const FILE *out, const char *now, const Entry *entries, size_t count,
                         const StoreNeighbor *links, size_t nlinks)
{
    if (out == NULL || now == NULL) {
        return false;
    }
    if (count > 0U && entries == NULL) {
        return false;
    }
    if (nlinks > 0U && links == NULL) {
        return false;
    }
    return true;
}

int output_list_envelope(FILE *out, size_t offset, size_t limit, size_t count, size_t total,
                         const Entry *entries, const StoreNeighbor *links, size_t nlinks,
                         const char *now)
{
    if (!page_args_ok(out, now, entries, count, links, nlinks)) {
        return -1;
    }
    if (write_page_head(out, offset, limit, count, total) != 0) {
        return -1;
    }
    return write_page_entries(out, entries, count, links, nlinks, now);
}

/* Each facet holds the items its count says. */
static bool facets_ok(const SearchResult *res)
{
    if (res->keys.count > 0U && res->keys.items == NULL) {
        return false;
    }
    if (res->tags.count > 0U && res->tags.items == NULL) {
        return false;
    }
    return true;
}

static int write_key_item(FILE *out, const SearchKeyItem *item)
{
    const JsonField rest[] = {
        {.name = "sync_id", .value = item->sync_id},
        {.name = "label", .value = item->label},
        {.name = "source", .value = item->source},
        {.name = "updated_at", .value = item->updated_at},
    };
    size_t i = 0;

    if (fputs("{\"key\":", out) < 0 || output_json_string(out, item->key) != 0) {
        return -1;
    }
    for (i = 0; i < sizeof rest / sizeof rest[0]; i++) {
        if (write_json_field_str(out, rest[i]) != 0) {
            return -1;
        }
    }
    return fputc('}', out) == EOF ? -1 : 0;
}

static int write_key_facet(FILE *out, const SearchKeyFacet *keys)
{
    size_t i = 0;

    if (fprintf(out, ",\"keys\":{\"total\":%zu,\"items\":[", keys->total) < 0) {
        return -1;
    }
    for (i = 0; i < keys->count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (write_key_item(out, &keys->items[i]) != 0) {
            return -1;
        }
    }
    return fputs("]}", out) < 0 ? -1 : 0;
}

static int write_tag_facet(FILE *out, const SearchTagFacet *tags)
{
    size_t i = 0;

    if (fprintf(out, ",\"tags\":{\"total\":%zu,\"items\":[", tags->total) < 0) {
        return -1;
    }
    for (i = 0; i < tags->count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (fputs("{\"tag\":", out) < 0 || output_json_string(out, tags->items[i].tag) != 0 ||
            fprintf(out, ",\"count\":%zu}", tags->items[i].count) < 0) {
            return -1;
        }
    }
    return fputs("]}", out) < 0 ? -1 : 0;
}

int output_search_envelope(FILE *out, const SearchResult *res, size_t offset, size_t limit,
                           const StoreNeighbor *links, size_t nlinks, const char *now)
{
    const PageResult *page = NULL;

    if (res == NULL) {
        return -1;
    }
    page = &res->page;
    if (!page_args_ok(out, now, page->entries, page->count, links, nlinks) || !facets_ok(res)) {
        return -1;
    }
    if (write_page_head(out, offset, limit, page->count, page->total) != 0) {
        return -1;
    }
    if (write_key_facet(out, &res->keys) != 0 || write_tag_facet(out, &res->tags) != 0) {
        return -1;
    }
    return write_page_entries(out, page->entries, page->count, links, nlinks, now);
}

enum { HUMAN_FACET_CAP = 10U }; /* E2.7 */

/* Shown items are capped at E2.7's 10 and at what the facet holds. */
static size_t facet_shown(size_t count)
{
    return count < HUMAN_FACET_CAP ? count : HUMAN_FACET_CAP;
}

/* Line end: ", ... and N more" when the facet has more matches than shown. */
static int write_facet_line_end(FILE *out, size_t shown, size_t total)
{
    if (total > shown && fprintf(out, ", \xe2\x80\xa6 and %zu more", total - shown) < 0) {
        return -1;
    }
    return fputc('\n', out) == EOF ? -1 : 0;
}

static int write_keys_human(FILE *out, const SearchKeyFacet *keys)
{
    size_t shown = facet_shown(keys->count);
    size_t i = 0;

    if (shown == 0U) {
        return 0;
    }
    if (fputs("keys: ", out) < 0) {
        return -1;
    }
    for (i = 0; i < shown; i++) {
        if ((i > 0U && fputs(", ", out) < 0) ||
            output_text_human(out, keys->items[i].key, false) != 0) {
            return -1;
        }
    }
    return write_facet_line_end(out, shown, keys->total);
}

static int write_tags_human(FILE *out, const SearchTagFacet *tags)
{
    size_t shown = facet_shown(tags->count);
    size_t i = 0;

    if (shown == 0U) {
        return 0;
    }
    if (fputs("tags: ", out) < 0) {
        return -1;
    }
    for (i = 0; i < shown; i++) {
        if ((i > 0U && fputs(", ", out) < 0) ||
            output_text_human(out, tags->items[i].tag, false) != 0 ||
            fprintf(out, " (%zu)", tags->items[i].count) < 0) {
            return -1;
        }
    }
    return write_facet_line_end(out, shown, tags->total);
}

int output_search_facets_human(FILE *out, const SearchResult *res)
{
    if (out == NULL || res == NULL || !facets_ok(res)) {
        return -1;
    }
    if (write_keys_human(out, &res->keys) != 0) {
        return -1;
    }
    return write_tags_human(out, &res->tags);
}

int output_id_human(FILE *out, long long id)
{
    if (out == NULL) {
        return -1;
    }
    if (fprintf(out, "%lld\n", id) < 0) {
        return -1;
    }
    return 0;
}

/* Control byte that could drive the terminal. Tab is safe whitespace. A
   multi-line body keeps \n; a one-line notice does not
   (decision:plan17a-notice-shared-classifier). Shared intent with
   write_preview's '?' mapping. */
static int is_terminal_ctrl(unsigned char c, bool keep_newline)
{
    if (c == (unsigned char)'\t' || (keep_newline && c == (unsigned char)'\n')) {
        return 0;
    }
    return c < ASCII_FIRST_PRINTABLE || c == ASCII_DEL;
}

/* A UTF-8 C1 control (U+0080..U+009F, bytes C2 80..C2 9F) starts at p. Some
   terminals honor U+009B as CSI (decision:plan18c-mask-c1). p[1] is read only
   after p[0] matched, so a NUL ends the string first. */
static bool is_c1_control_at(const unsigned char *p)
{
    if (p[0] == UTF8_C1_LEAD && p[1] >= UTF8_C1_FIRST && p[1] <= UTF8_C1_LAST) {
        return true;
    }
    return false;
}

int output_text_human(FILE *out, const char *text, bool keep_newline)
{
    const unsigned char *p = NULL;

    if (text == NULL) {
        return 0;
    }
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        int ch = (int)*p;

        if (is_c1_control_at(p)) {
            ch = '?';
            p++; /* both bytes of the control become one '?' */
        } else if (is_terminal_ctrl(*p, keep_newline)) {
            ch = '?';
        }
        if (fputc(ch, out) == EOF) {
            return -1;
        }
    }
    return 0;
}

int output_body_human(FILE *out, const char *body)
{
    if (out == NULL) {
        return -1;
    }
    if (output_text_human(out, body, true) != 0) {
        return -1;
    }
    if (fputc('\n', out) == EOF) {
        return -1;
    }
    return 0;
}

static size_t utf8_clen(unsigned char c)
{
    if ((c & UTF8_HIGH_BIT) == 0U) {
        return 1U;
    }
    if ((c & UTF8_LEAD2_MASK) == UTF8_LEAD2_TAG) {
        return 2U;
    }
    if ((c & UTF8_LEAD3_MASK) == UTF8_LEAD3_TAG) {
        return 3U;
    }
    if ((c & UTF8_LEAD4_MASK) == UTF8_LEAD4_TAG) {
        return 4U;
    }
    return 1U;
}

/* Byte length of the UTF-8 sequence at body[i], clamped so a truncated
   trailing sequence never reads past the NUL. Bodies are validated UTF-8 on
   add; this keeps the scan in-bounds regardless. */
static size_t utf8_clen_bounded(const char *body, size_t i)
{
    size_t clen = utf8_clen((unsigned char)body[i]);
    size_t k = 0;

    for (k = 1U; k < clen; k++) {
        if (body[i + k] == '\0') {
            return 1U;
        }
    }
    return clen;
}

enum { LIST_PREVIEW_CP = 80U, STUB_PREVIEW_CP = 40U, PREVIEW_BUF = 192U };

static void fill_preview(char dst[PREVIEW_BUF], const char *body, size_t max_cp);

/* First line, max 80 codepoints, ellipsis if truncated. Control bytes → '?'. */
static int write_preview(FILE *out, const char *body)
{
    char buf[PREVIEW_BUF];

    fill_preview(buf, body, LIST_PREVIEW_CP);
    return fputs(buf, out) < 0 ? -1 : 0;
}

int output_tags_envelope(FILE *out, const TagCount *tags, size_t count)
{
    size_t i = 0;

    if (out == NULL) {
        return -1;
    }
    if (count > 0U && tags == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"count\":%zu,\"tags\":[", count) < 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (fputs("{\"name\":", out) < 0) {
            return -1;
        }
        if (output_json_string(out, tags[i].name) != 0) {
            return -1;
        }
        if (fprintf(out, ",\"count\":%lld}", tags[i].count) < 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_tags_human(FILE *out, const TagCount *tags, size_t count)
{
    size_t i = 0;

    if (out == NULL) {
        return -1;
    }
    if (count > 0U && tags == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        /* Masked: stored text may come from a peer (decision:import-control-bytes-followup). */
        if (output_text_human(out, tags[i].name, false) != 0 ||
            fprintf(out, "\t%lld\n", tags[i].count) < 0) {
            return -1;
        }
    }
    return 0;
}

enum { HUMAN_RELATED_CAP = 5U };

static int write_related_ids_cell(FILE *out, long long subject_id, const StoreNeighbor *links,
                                  size_t nlinks, const char *now);

int output_entry_human_line(FILE *out, const Entry *e, const StoreNeighbor *links, size_t nlinks,
                            const char *now)
{
    size_t i = 0;

    if (out == NULL || e == NULL) {
        return -1;
    }
    if (nlinks > 0U && links == NULL) {
        return -1;
    }
    if (fprintf(out, "%lld | ", e->id) < 0) {
        return -1;
    }
    /* Masked: stored text may come from a peer (decision:import-control-bytes-followup). */
    if (output_text_human(out, e->key, false) != 0 || fputs(" | ", out) < 0) {
        return -1;
    }
    for (i = 0; i < e->ntags; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_text_human(out, e->tags[i], false) != 0) {
            return -1;
        }
    }
    if (fputs(" | ", out) < 0 || write_preview(out, e->body) != 0 || fputs(" | ", out) < 0) {
        return -1;
    }
    if (output_text_human(out, e->updated_at, false) != 0 || fputs(" | ", out) < 0) {
        return -1;
    }
    if (write_related_ids_cell(out, e->id, links, nlinks, now) != 0) {
        return -1;
    }
    return fputc('\n', out) == EOF ? -1 : 0;
}

static const char *link_type_token(const StoreNeighbor *n)
{
    if (n->kind == STORE_EDGE_RELATED) {
        return "related";
    }
    if (n->kind == STORE_EDGE_CITES) {
        return (n->subject_id == n->from_id) ? "cites" : "cited_by";
    }
    return (n->subject_id == n->from_id) ? "supersedes" : "superseded_by";
}

static StoreBin neighbor_bin(const StoreNeighbor *n, const char *now)
{
    return store_bin_of(n->neighbor_deleted_at, n->neighbor_expires_at, now);
}

static const char *human_bin_mark(StoreBin bin)
{
    if (bin == STORE_BIN_EXPIRED) {
        return "[expired]";
    }
    if (bin == STORE_BIN_DELETED) {
        return "[deleted]";
    }
    return NULL;
}

static void fill_preview(char dst[PREVIEW_BUF], const char *body, size_t max_cp)
{
    size_t i = 0U;
    size_t o = 0U;
    size_t cps = 0U;
    int truncated = 0;

    dst[0] = '\0';
    if (body == NULL) {
        return;
    }
    while (body[i] != '\0' && body[i] != '\n' && body[i] != '\r') {
        unsigned char c = (unsigned char)body[i];
        size_t clen = utf8_clen_bounded(body, i);
        if (cps >= max_cp) {
            truncated = 1;
            break;
        }
        if (o + clen + 4U >= PREVIEW_BUF) {
            truncated = 1;
            break;
        }
        if (c < ASCII_FIRST_PRINTABLE || c == ASCII_DEL ||
            is_c1_control_at((const unsigned char *)body + i)) {
            dst[o++] = '?';
        } else {
            size_t k = 0;
            /* utf8_clen_bounded already stops clen before a NUL. */
            for (k = 0; k < clen; k++) {
                dst[o++] = body[i + k];
            }
        }
        i += clen;
        cps++;
    }
    /* o starts at 0 and each copy leaves o + 4 < PREVIEW_BUF, so the dots and the NUL fit. */
    if (truncated) {
        dst[o++] = '.';
        dst[o++] = '.';
        dst[o++] = '.';
    }
    dst[o] = '\0';
}

static int write_stub_json(FILE *out, const StoreNeighbor *n, const char *now)
{
    char preview[PREVIEW_BUF];
    const char *bin = NULL;

    if (n->neighbor_sync_id == NULL) {
        return -1;
    }
    bin = BIN_TOKENS[neighbor_bin(n, now)];
    fill_preview(preview, n->neighbor_body, STUB_PREVIEW_CP);
    if (fprintf(out, "{\"id\":%lld,\"sync_id\":", n->neighbor_id) < 0) {
        return -1;
    }
    if (output_json_string(out, n->neighbor_sync_id) != 0) {
        return -1;
    }
    if (write_json_nullable(out, "key", n->neighbor_key) != 0) {
        return -1;
    }
    if (fputs(",\"type\":", out) < 0) {
        return -1;
    }
    if (output_json_string(out, link_type_token(n)) != 0) {
        return -1;
    }
    if (write_json_field_str(out, (JsonField){.name = "bin", .value = bin}) != 0) {
        return -1;
    }
    if (write_json_field_str(out, (JsonField){.name = "preview", .value = preview}) != 0) {
        return -1;
    }
    if (fputc('}', out) == EOF) {
        return -1;
    }
    return 0;
}

static int write_links_json_field(FILE *out, long long subject_id, const StoreNeighbor *links,
                                  size_t nlinks, const char *now)
{
    size_t i = 0;
    int first = 1;

    if (fputs(",\"links\":[", out) < 0) {
        return -1;
    }
    for (i = 0; i < nlinks; i++) {
        if (links[i].subject_id != subject_id) {
            continue;
        }
        if (!first && fputc(',', out) == EOF) {
            return -1;
        }
        first = 0;
        if (write_stub_json(out, &links[i], now) != 0) {
            return -1;
        }
    }
    if (fputc(']', out) == EOF) {
        return -1;
    }
    return 0;
}

static int write_entry_with_links(FILE *out, const Entry *e, const StoreNeighbor *links,
                                  size_t nlinks, const char *now)
{
    if (write_entry_core(out, e, now) != 0) {
        return -1;
    }
    if (write_links_json_field(out, e->id, links, nlinks, now) != 0) {
        return -1;
    }
    if (fputc('}', out) == EOF) {
        return -1;
    }
    return 0;
}

static int write_related_ids_cell(FILE *out, long long subject_id, const StoreNeighbor *links,
                                  size_t nlinks, const char *now)
{
    size_t i = 0;
    size_t shown = 0U;
    size_t total = 0U;

    if (nlinks == 0U) {
        return 0;
    }
    for (i = 0; i < nlinks; i++) {
        if (links[i].subject_id == subject_id) {
            total++;
        }
    }
    for (i = 0; i < nlinks; i++) {
        if (links[i].subject_id != subject_id) {
            continue;
        }
        if (shown >= HUMAN_RELATED_CAP) {
            break;
        }
        if (shown > 0U && fputs(", ", out) < 0) {
            return -1;
        }
        if (fprintf(out, "%lld", links[i].neighbor_id) < 0) {
            return -1;
        }
        {
            const char *mark = human_bin_mark(neighbor_bin(&links[i], now));
            if (mark != NULL && fputs(mark, out) < 0) {
                return -1;
            }
        }
        shown++;
    }
    if (total > HUMAN_RELATED_CAP) {
        if (fprintf(out, ", +%zu", total - HUMAN_RELATED_CAP) < 0) {
            return -1;
        }
    }
    return 0;
}

int output_links_write_envelope(FILE *out, const char *action, const StoreNeighbor *links,
                                size_t count, const char *now)
{
    size_t i = 0;

    if (out == NULL || action == NULL || now == NULL) {
        return -1;
    }
    if (count > 0U && links == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"action\":") < 0) {
        return -1;
    }
    if (output_json_string(out, action) != 0) {
        return -1;
    }
    if (fprintf(out, ",\"count\":%zu,\"links\":[", count) < 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (write_stub_json(out, &links[i], now) != 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_related_envelope(FILE *out, long long id, const char *key, const StoreNeighbor *links,
                            size_t count, const char *now)
{
    size_t i = 0;

    if (out == NULL || now == NULL) {
        return -1;
    }
    if (count > 0U && links == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"id\":%lld,\"key\":", id) < 0) {
        return -1;
    }
    if (key == NULL) {
        if (fputs("null", out) < 0) {
            return -1;
        }
    } else if (output_json_string(out, key) != 0) {
        return -1;
    }
    if (fprintf(out, ",\"count\":%zu,\"links\":[", count) < 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (write_stub_json(out, &links[i], now) != 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_related_human(FILE *out, const StoreNeighbor *links, size_t count, const char *now)
{
    size_t i = 0;
    char preview[PREVIEW_BUF];

    if (out == NULL || now == NULL) {
        return -1;
    }
    if (count == 0U) {
        return 0;
    }
    if (links == NULL) {
        return -1;
    }
    if (fputs("Related:\n", out) < 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const StoreNeighbor *n = &links[i];
        fill_preview(preview, n->neighbor_body, STUB_PREVIEW_CP);
        if (fprintf(out, "%s ", link_type_token(n)) < 0) {
            return -1;
        }
        if (n->neighbor_key != NULL) {
            if (output_text_human(out, n->neighbor_key, false) != 0) {
                return -1;
            }
        } else if (fprintf(out, "%lld", n->neighbor_id) < 0) {
            return -1;
        }
        {
            const char *mark = human_bin_mark(neighbor_bin(n, now));
            if (mark != NULL && fprintf(out, " %s", mark) < 0) {
                return -1;
            }
        }
        if (preview[0] != '\0' && fprintf(out, " %s", preview) < 0) {
            return -1;
        }
        if (fputc('\n', out) == EOF) {
            return -1;
        }
    }
    return 0;
}

static int write_device_id_field(FILE *out, const char *device_id)
{
    if (fputs("{\"version\":1,\"device_id\":", out) < 0) {
        return -1;
    }
    return output_json_string(out, device_id);
}

int output_device_human(FILE *out, const char *device_id)
{
    if (out == NULL || device_id == NULL) {
        return -1;
    }
    if (fprintf(out, "%s\n", device_id) < 0) {
        return -1;
    }
    return 0;
}

int output_device_envelope(FILE *out, const char *device_id)
{
    if (out == NULL || device_id == NULL) {
        return -1;
    }
    if (write_device_id_field(out, device_id) != 0) {
        return -1;
    }
    if (fputs("}\n", out) < 0) {
        return -1;
    }
    return 0;
}

/* device_id then previous: the new id, then the id it replaced (NULL is JSON null). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int output_device_new_envelope(FILE *out, const char *device_id, const char *previous)
{
    if (out == NULL || device_id == NULL) {
        return -1;
    }
    if (write_device_id_field(out, device_id) != 0) {
        return -1;
    }
    if (fputs(",\"previous\":", out) < 0) {
        return -1;
    }
    if (previous != NULL) {
        if (output_json_string(out, previous) != 0) {
            return -1;
        }
    } else if (fputs("null", out) < 0) {
        return -1;
    }
    if (fputs("}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_import_envelope(FILE *out, const StoreImportCounts *counts)
{
    if (out == NULL || counts == NULL) {
        return -1;
    }
    if (fprintf(out,
                "{\"version\":1,\"action\":\"imported\",\"inserted\":%zu,\"updated\":%zu,"
                "\"unchanged\":%zu,\"conflicts\":%zu,\"rejected\":%zu}\n",
                counts->inserted, counts->updated, counts->unchanged, counts->conflicts,
                counts->rejected) < 0) {
        return -1;
    }
    return 0;
}

static int output_one_conflict_json(FILE *out, const StoreConflict *row)
{
    const char *reason = NULL;

    reason = store_conflict_reason_str(row->reason);
    if (reason == NULL || row->sync_id == NULL || row->local_json == NULL ||
        row->incoming_json == NULL || row->created_at == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"id\":%lld,\"sync_id\":", row->id) < 0) {
        return -1;
    }
    if (output_json_string(out, row->sync_id) != 0) {
        return -1;
    }
    if (fprintf(out, ",\"reason\":") < 0) {
        return -1;
    }
    if (output_json_string(out, reason) != 0) {
        return -1;
    }
    if (fprintf(out, ",\"local\":%s,\"incoming\":%s,\"created_at\":", row->local_json,
                row->incoming_json) < 0) {
        return -1;
    }
    if (output_json_string(out, row->created_at) != 0) {
        return -1;
    }
    return (fputc('}', out) == EOF) ? -1 : 0;
}

int output_conflicts_envelope(FILE *out, const StoreConflict *rows, size_t count)
{
    size_t i = 0;

    if (out == NULL) {
        return -1;
    }
    if (count > 0U && rows == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"count\":%zu,\"conflicts\":[", count) < 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_one_conflict_json(out, &rows[i]) != 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_accepted_envelope(FILE *out, const Entry *entries, size_t count, const char *now)
{
    size_t i = 0;

    if (out == NULL || now == NULL) {
        return -1;
    }
    if (count > 0U && entries == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"action\":\"accepted\",\"count\":%zu,\"entries\":[", count) <
        0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_entry_json(out, &entries[i], now) != 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_rebuilt(FILE *out, bool json, size_t entries)
{
    int n = 0;

    if (out == NULL) {
        return -1;
    }
    if (json) {
        n = fprintf(out, "{\"version\":1,\"action\":\"rebuilt\",\"entries\":%zu}\n", entries);
    } else {
        n = fprintf(out, "rebuilt %zu\n", entries);
    }
    if (n < 0) {
        return -1;
    }
    return 0;
}
