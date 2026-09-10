#include "output.h"

#include "store.h"

#include <stdio.h>
#include <string.h>

/* ASCII / UTF-8 byte constants (see also normalize.c). Unsigned to keep the
   mask arithmetic unsigned. */
static const unsigned ASCII_FIRST_PRINTABLE = 0x20U; /* first non-control byte */
static const unsigned ASCII_DEL = 0x7FU;             /* DEL control */
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
    return output_json_string(out, field.value != NULL ? field.value : empty_str());
}

static int write_entry_core(FILE *out, const Entry *e)
{
    size_t i = 0;

    if (fprintf(out, "{\"id\":%lld,\"key\":", e->id) < 0) {
        return -1;
    }
    if (e->key == NULL) {
        if (fputs("null", out) < 0) {
            return -1;
        }
    } else if (output_json_string(out, e->key) != 0) {
        return -1;
    }
    if (write_json_field_str(out, (JsonField){.name = "body", .value = e->body}) != 0) {
        return -1;
    }
    if (fputs(",\"tags\":[", out) < 0) {
        return -1;
    }
    for (i = 0; i < e->ntags; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_json_string(out, e->tags[i]) != 0) {
            return -1;
        }
    }
    if (fputs("]", out) < 0) {
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
    if (e->expires_at == NULL) {
        if (fputs(",\"expires_at\":null", out) < 0) {
            return -1;
        }
    } else if (write_json_field_str(
                   out, (JsonField){.name = "expires_at", .value = e->expires_at}) != 0) {
        return -1;
    }
    return 0;
}

/* cppcheck-suppress staticFunction ; public API (output.h); used by tests */
int output_entry_json(FILE *out, const Entry *e)
{
    if (out == NULL || e == NULL) {
        return -1;
    }
    if (write_entry_core(out, e) != 0) {
        return -1;
    }
    if (fputc('}', out) == EOF) {
        return -1;
    }
    return 0;
}

static int write_entry_with_links(FILE *out, const Entry *e, const StoreNeighbor *links,
                                  size_t nlinks, const char *now);

int output_action_envelope(FILE *out, const char *action, const Entry *e)
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
    if (output_entry_json(out, e) != 0) {
        return -1;
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_deleted_list(FILE *out, const Entry *entries, size_t count)
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
        if (output_entry_json(out, &entries[i]) != 0) {
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

int output_list_envelope(FILE *out, size_t offset, size_t limit, size_t count, size_t total,
                         const Entry *entries, const StoreNeighbor *links, size_t nlinks,
                         const char *now)
{
    size_t i = 0;

    if (out == NULL || now == NULL) {
        return -1;
    }
    if (count > 0U && entries == NULL) {
        return -1;
    }
    if (nlinks > 0U && links == NULL) {
        return -1;
    }
    if (fprintf(out,
                "{\"version\":1,\"offset\":%zu,\"limit\":%zu,\"count\":%zu,\"total\":%zu,"
                "\"entries\":[",
                offset, limit, count, total) < 0) {
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

/* Control byte that could drive the terminal (drop \n and \t, which are safe
   whitespace we want to keep). Shared intent with write_preview's '?' mapping. */
static int is_terminal_ctrl(unsigned char c)
{
    if (c == (unsigned char)'\n' || c == (unsigned char)'\t') {
        return 0;
    }
    return c < ASCII_FIRST_PRINTABLE || c == ASCII_DEL;
}

int output_body_human(FILE *out, const char *body)
{
    if (out == NULL) {
        return -1;
    }
    if (body != NULL) {
        const unsigned char *p = NULL;
        for (p = (const unsigned char *)body; *p != '\0'; p++) {
            int ch = is_terminal_ctrl(*p) ? '?' : (int)*p;
            if (fputc(ch, out) == EOF) {
                return -1;
            }
        }
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

static void fill_preview(char *dst, size_t dst_cap, const char *body, size_t max_cp);

/* First line, max 80 codepoints, ellipsis if truncated. Control bytes → '?'. */
static void write_preview(FILE *out, const char *body)
{
    char buf[PREVIEW_BUF];

    fill_preview(buf, sizeof(buf), body, LIST_PREVIEW_CP);
    (void)fputs(buf, out);
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
        /* Names are normalize_token output: no ASCII controls, raw print is safe. */
        if (fprintf(out, "%s\t%lld\n", tags[i].name != NULL ? tags[i].name : empty_str(),
                    tags[i].count) < 0) {
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
    /* Keys/tags are normalize_token output: no ASCII controls. Raw fputs is safe. */
    if (e->key != NULL) {
        (void)fputs(e->key, out);
    }
    (void)fputs(" | ", out);
    for (i = 0; i < e->ntags; i++) {
        if (i > 0U) {
            (void)fputc(',', out);
        }
        if (e->tags[i] != NULL) {
            (void)fputs(e->tags[i], out);
        }
    }
    (void)fputs(" | ", out);
    write_preview(out, e->body);
    (void)fputs(" | ", out);
    if (e->updated_at != NULL) {
        (void)fputs(e->updated_at, out);
    }
    (void)fputs(" | ", out);
    if (write_related_ids_cell(out, e->id, links, nlinks, now) != 0) {
        return -1;
    }
    (void)fputc('\n', out);
    return 0;
}

static const char *link_type_token(const StoreNeighbor *n)
{
    if (n == NULL) {
        return "related";
    }
    if (n->kind == STORE_EDGE_RELATED) {
        return "related";
    }
    if (n->kind == STORE_EDGE_CITES) {
        return (n->subject_id == n->from_id) ? "cites" : "cited_by";
    }
    if (n->kind == STORE_EDGE_SUPERSEDES) {
        return (n->subject_id == n->from_id) ? "supersedes" : "superseded_by";
    }
    return "related";
}

static int neighbor_trash(const StoreNeighbor *n, const char *now)
{
    if (n == NULL || n->neighbor_expires_at == NULL || now == NULL) {
        return 0;
    }
    return strcmp(n->neighbor_expires_at, now) <= 0;
}

static void fill_preview(char *dst, size_t dst_cap, const char *body, size_t max_cp)
{
    size_t i = 0U;
    size_t o = 0U;
    size_t cps = 0U;
    int truncated = 0;

    if (dst == NULL || dst_cap == 0U) {
        return;
    }
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
        if (o + clen + 4U >= dst_cap) {
            truncated = 1;
            break;
        }
        if (c < ASCII_FIRST_PRINTABLE || c == ASCII_DEL) {
            dst[o++] = '?';
        } else {
            size_t k = 0;
            for (k = 0; k < clen && body[i + k] != '\0'; k++) {
                dst[o++] = body[i + k];
            }
        }
        i += clen;
        cps++;
    }
    if (body[i] != '\0' && body[i] != '\n' && body[i] != '\r') {
        truncated = 1;
    }
    if (truncated && o + 3U < dst_cap) {
        dst[o++] = '.';
        dst[o++] = '.';
        dst[o++] = '.';
    }
    dst[o] = '\0';
}

static int write_stub_json(FILE *out, const StoreNeighbor *n, const char *now)
{
    char preview[PREVIEW_BUF];
    int trash = 0;

    if (n == NULL) {
        return -1;
    }
    trash = neighbor_trash(n, now);
    fill_preview(preview, sizeof(preview), n->neighbor_body, STUB_PREVIEW_CP);
    if (fprintf(out, "{\"id\":%lld,\"key\":", n->neighbor_id) < 0) {
        return -1;
    }
    if (n->neighbor_key == NULL) {
        if (fputs("null", out) < 0) {
            return -1;
        }
    } else if (output_json_string(out, n->neighbor_key) != 0) {
        return -1;
    }
    if (fputs(",\"type\":", out) < 0) {
        return -1;
    }
    if (output_json_string(out, link_type_token(n)) != 0) {
        return -1;
    }
    if (fprintf(out, ",\"trash\":%s,\"preview\":", trash ? "true" : "false") < 0) {
        return -1;
    }
    if (output_json_string(out, preview) != 0) {
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
    if (write_entry_core(out, e) != 0) {
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
        if (neighbor_trash(&links[i], now) && fputs("[trash]", out) < 0) {
            return -1;
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
        fill_preview(preview, sizeof(preview), n->neighbor_body, STUB_PREVIEW_CP);
        if (fprintf(out, "%s ", link_type_token(n)) < 0) {
            return -1;
        }
        if (n->neighbor_key != NULL) {
            if (fputs(n->neighbor_key, out) < 0) {
                return -1;
            }
        } else if (fprintf(out, "%lld", n->neighbor_id) < 0) {
            return -1;
        }
        if (neighbor_trash(n, now) && fputs(" [trash]", out) < 0) {
            return -1;
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
