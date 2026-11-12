#include "export_format.h"

#include "normalize.h"
#include "output.h"
#include "search_terms.h"
#include "settings.h"
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_bin_names[] = {
    [STORE_BIN_LIVE] = "live",
    [STORE_BIN_EXPIRED] = "expired",
    [STORE_BIN_DELETED] = "deleted",
};

static const char *const k_md_folders[] = {
    [STORE_BIN_LIVE] = "memories",
    [STORE_BIN_EXPIRED] = "expired",
    [STORE_BIN_DELETED] = "deleted",
};

enum { SLUG_MAX = 48, SLUG_WORDS = 6, ID_TAIL = 12 };

const char *export_md_folder(StoreBin bin)
{
    return k_md_folders[bin];
}

static int cmp_items(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* A canonical vector has no comma inside an item (a UUID key and digits), so
   the items split on commas, and every key has the same length, so sorting
   whole items sorts the keys. */
/* The compact vector with its keys in byte order (D18), heap. NULL when it is
   not canonical or memory runs out. */
static char *export_vv_sorted(const char *vector)
{
    size_t len = 0;
    char *inner = NULL;
    char **items = NULL;
    size_t n = 0;
    size_t i = 0;
    char *out = NULL;
    char *w = NULL;

    if (vector == NULL || !vv_json_is_canonical(vector)) {
        return NULL;
    }
    len = strlen(vector);
    inner = strdup(vector);
    items = (char **)calloc(len, sizeof(*items));
    out = (char *)malloc(len + 1U);
    if (inner == NULL || items == NULL || out == NULL) {
        goto fail;
    }
    inner[len - 1U] = '\0'; /* drop the closing brace; the walk skips the opening one */
    for (w = inner + 1; *w != '\0'; n++) {
        items[n] = w;
        w += strcspn(w, ",");
        if (*w == ',') {
            *w++ = '\0';
        }
    }
    qsort((void *)items, n, sizeof(*items), cmp_items);
    w = out;
    *w++ = '{';
    for (i = 0; i < n; i++) {
        size_t k = strlen(items[i]);

        if (i > 0U) {
            *w++ = ',';
        }
        memcpy(w, items[i], k);
        w += k;
    }
    *w++ = '}';
    *w = '\0';
    free((void *)items);
    free(inner);
    return out;

fail:
    free((void *)items);
    free(inner);
    free(out);
    return NULL;
}

/* {"format":...,"bins":{...} with no closing brace. */
static int write_header(FILE *out, const ExportDoc *doc)
{
    size_t b = 0;
    const char *sep = "";

    if (fputs("{\"format\":\"remember-export\",\"version\":1,\"brain_id\":", out) < 0 ||
        output_json_string(out, doc->snap->brain_id) != 0 || fputs(",\"exported_at\":", out) < 0 ||
        output_json_string(out, doc->exported_at) != 0 || fputs(",\"engine\":", out) < 0 ||
        output_json_string(out, doc->engine) != 0 ||
        fprintf(out, ",\"schema\":%d,\"bins\":{", doc->snap->schema) < 0) {
        return -1;
    }
    for (b = 0; b <= (size_t)STORE_BIN_DELETED; b++) {
        if (!doc->bins.in[b]) {
            continue;
        }
        if (fprintf(out, "%s\"%s\":%zu", sep, k_bin_names[b], doc->snap->counts[b]) < 0) {
            return -1;
        }
        sep = ",";
    }
    return fputc('}', out) == EOF ? -1 : 0;
}

/* The entry with its vector's keys sorted. */
static int write_entry(FILE *out, const Entry *e, const char *now)
{
    Entry sorted = *e;
    int rc = 0;

    sorted.version_vector = export_vv_sorted(e->version_vector);
    if (sorted.version_vector == NULL) {
        return -1;
    }
    rc = output_export_entry_json(out, &sorted, now);
    free(sorted.version_vector);
    return rc;
}

/* A link object. with_from is false in a memory's own links (E6.b4, D25). */
static int write_link(FILE *out, const StoreExportLink *l, bool with_from)
{
    if (fputc('{', out) == EOF) {
        return -1;
    }
    if (with_from && (fputs("\"from\":", out) < 0 || output_json_string(out, l->from) != 0 ||
                      fputc(',', out) == EOF)) {
        return -1;
    }
    if (fputs("\"to\":", out) < 0 || output_json_string(out, l->to) != 0 ||
        fputs(",\"kind\":", out) < 0 || output_json_string(out, l->kind) != 0 ||
        fputs(",\"created_at\":", out) < 0 || output_json_string(out, l->created_at) != 0 ||
        fputs(",\"updated_at\":", out) < 0 || output_json_string(out, l->updated_at) != 0) {
        return -1;
    }
    return fputc('}', out) == EOF ? -1 : 0;
}

static const char *settings_scope(SettingLayer layer)
{
    if (layer == SETTING_LAYER_BRAIN) {
        return "brain";
    }
    return "user";
}

/* A registered duration also carries value_ms (ST-8.2). */
static bool settings_value_ms(const SettingExportRow *row, long long *ms)
{
    const SettingSpec *spec = setting_by_key(row->key);
    char inner[SETTING_VALUE_CAP];
    char canon[NORM_MEASURE_CANON_CAP];

    if (spec == NULL || spec->type != SETTING_DURATION ||
        !setting_quoted_plain(row->value, inner, sizeof(inner))) {
        return false;
    }
    return norm_duration_canonical(inner, ms, canon, sizeof(canon));
}

/* ST-8.2 member order, value_ms after value. */
static int write_settings_row(FILE *out, const SettingExportRow *row)
{
    long long ms = 0;

    if (fputs("{\"key\":", out) < 0 || output_json_string(out, row->key) != 0 ||
        fprintf(out, ",\"scope\":\"%s\",\"value\":%s", settings_scope(row->layer), row->value) <
            0) {
        return -1;
    }
    if (settings_value_ms(row, &ms) && fprintf(out, ",\"value_ms\":%lld", ms) < 0) {
        return -1;
    }
    if (fprintf(out, ",\"updated_by\":%s,\"updated_at\":", row->updated_by) < 0 ||
        output_json_string(out, row->updated_at) != 0 || fputs(",\"device_id\":", out) < 0 ||
        output_json_string(out, row->device_id) != 0 ||
        fprintf(out, ",\"version_vector\":%s,\"deleted_at\":", row->version_vector) < 0) {
        return -1;
    }
    if (row->deleted_at == NULL) {
        return fputs("null}", out) < 0 ? -1 : 0;
    }
    if (output_json_string(out, row->deleted_at) != 0) {
        return -1;
    }
    return fputc('}', out) == EOF ? -1 : 0;
}

int export_settings_write(FILE *out, const char *exported_at, const SettingExportRow *rows,
                          size_t n, bool trailing_newline)
{
    size_t i = 0;
    const char *tail = "]}";

    if (fprintf(out,
                "{\"format\":\"remember-settings-export\",\"version\":1,\"exported_at\":\"%s\","
                "\"settings\":[",
                exported_at) < 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if ((i > 0U && fputc(',', out) == EOF) || write_settings_row(out, &rows[i]) != 0) {
            return -1;
        }
    }
    if (trailing_newline) {
        tail = "]}\n";
    }
    return fputs(tail, out) < 0 ? -1 : 0;
}

int export_json_write(FILE *out, const ExportDoc *doc)
{
    const StoreExportSnapshot *snap = doc->snap;
    size_t i = 0;

    if (write_header(out, doc) != 0 || fputs(",\"entries\":[", out) < 0) {
        return -1;
    }
    for (i = 0; i < snap->nentries; i++) {
        if ((i > 0U && fputc(',', out) == EOF) ||
            write_entry(out, &snap->entries[i], doc->exported_at) != 0) {
            return -1;
        }
    }
    if (fputs("],\"links\":[", out) < 0) {
        return -1;
    }
    for (i = 0; i < snap->nlinks; i++) {
        if ((i > 0U && fputc(',', out) == EOF) || write_link(out, &snap->links[i], true) != 0) {
            return -1;
        }
    }
    if (fputs("],\"settings\":", out) < 0 ||
        export_settings_write(out, doc->exported_at, snap->settings, snap->nsettings, false) != 0) {
        return -1;
    }
    return fputs("}\n", out) < 0 ? -1 : 0;
}

static bool slug_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return true;
    }
    if (c >= '0' && c <= '9') {
        return true;
    }
    return false;
}

/* E6.b2: ASCII-lowercase, each run outside [a-z0-9] one '-', no '-' at
   either end, at most 48 characters, "memory" when empty. */
static void slugify(const char *src, size_t n, char out[SLUG_MAX + 1])
{
    size_t w = 0;
    size_t i = 0;
    bool gap = false;

    for (i = 0; i < n && w < (size_t)SLUG_MAX; i++) {
        char c = src[i];

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (!slug_char(c)) {
            gap = true;
            continue;
        }
        if (gap && w > 0U) {
            out[w++] = '-';
        }
        gap = false;
        if (w < (size_t)SLUG_MAX) {
            out[w++] = c;
        }
    }
    while (w > 0U && out[w - 1U] == '-') {
        w--;
    }
    if (w == 0U) {
        (void)snprintf(out, SLUG_MAX + 1, "memory");
        return;
    }
    out[w] = '\0';
}

void export_md_name(const Entry *e, char out[EXPORT_MD_NAME_MAX])
{
    char slug[SLUG_MAX + 1];
    char tail[ID_TAIL + 1];
    size_t start = 0;
    size_t end = 0;
    size_t idlen = strlen(e->sync_id);
    const char *id_tail = e->sync_id + (idlen > (size_t)ID_TAIL ? idlen - ID_TAIL : 0U);
    size_t i = 0;

    if (e->key != NULL) {
        slugify(e->key, strlen(e->key), slug);
    } else if (search_title_span(e->body, &start, &end)) {
        slugify(e->body + start, end - start, slug);
    } else {
        /* Stored bodies are valid UTF-8; were one not, slug the whole body. */
        if (search_words_end(e->body, SLUG_WORDS, &end) != SEARCH_OK) {
            end = strlen(e->body);
        }
        slugify(e->body, end, slug);
    }
    /* A hand-edited id names no path: only [a-z0-9] of its tail reaches the name. */
    for (i = 0; id_tail[i] != '\0'; i++) {
        tail[i] = '-';
        if (slug_char(id_tail[i])) {
            tail[i] = id_tail[i];
        }
    }
    tail[i] = '\0';
    (void)snprintf(out, EXPORT_MD_NAME_MAX, "%s--%s.md", slug, tail);
}

static int cmp_link_from(const void *key, const void *elem)
{
    return strcmp((const char *)key, ((const StoreExportLink *)elem)->from);
}

/* The JSON array of e's own links: the snapshot's links whose from is id,
   found by binary search (the links sort by from). */
static int write_own_links(FILE *out, const StoreExportSnapshot *snap, const char *id)
{
    const StoreExportLink *hit = NULL;
    const StoreExportLink *end = snap->links + snap->nlinks;
    bool first = true;

    if (fputc('[', out) == EOF) {
        return -1;
    }
    if (snap->nlinks > 0U) {
        hit = (const StoreExportLink *)bsearch(id, snap->links, snap->nlinks, sizeof(*snap->links),
                                               cmp_link_from);
    }
    while (hit != NULL && hit > snap->links && strcmp(hit[-1].from, id) == 0) {
        hit--;
    }
    for (; hit != NULL && hit < end && strcmp(hit->from, id) == 0; hit++) {
        if ((!first && fputc(',', out) == EOF) || write_link(out, hit, false) != 0) {
            return -1;
        }
        first = false;
    }
    return fputc(']', out) == EOF ? -1 : 0;
}

static int write_json_or_null(FILE *out, const char *s)
{
    if (s == NULL) {
        return fputs("null", out) < 0 ? -1 : 0;
    }
    return output_json_string(out, s);
}

/* "<name>: " then the value's JSON is the caller's. */
static int field(FILE *out, const char *name)
{
    return fprintf(out, "%s: ", name) < 0 ? -1 : 0;
}

int export_md_write(FILE *out, const ExportDoc *doc, const Entry *e)
{
    char *vector = export_vv_sorted(e->version_vector);
    int rc = -1;

    if (vector == NULL || !actor_json_is_canonical(e->created_by) ||
        !actor_json_is_canonical(e->updated_by)) {
        goto done;
    }
    if (fputs("---\nremember_export: 1\n", out) < 0 || field(out, "id") != 0 ||
        output_json_string(out, e->sync_id) != 0 || fputs("\n", out) < 0 ||
        field(out, "key") != 0 || write_json_or_null(out, e->key) != 0 || fputs("\n", out) < 0 ||
        field(out, "tags") != 0 ||
        output_json_string_array(out, (const char *const *)e->tags, e->ntags) != 0 ||
        fprintf(out, "\ncreated_by: %s\nupdated_by: %s\n", e->created_by, e->updated_by) < 0 ||
        field(out, "created_at") != 0 || output_json_string(out, e->created_at) != 0 ||
        fputs("\n", out) < 0 || field(out, "updated_at") != 0 ||
        output_json_string(out, e->updated_at) != 0 || fputs("\n", out) < 0 ||
        field(out, "expires_at") != 0 || write_json_or_null(out, e->expires_at) != 0 ||
        fputs("\n", out) < 0 || field(out, "deleted_at") != 0 ||
        write_json_or_null(out, e->deleted_at) != 0 ||
        fprintf(out, "\nversion_vector: %s\n", vector) < 0 || field(out, "links") != 0 ||
        write_own_links(out, doc->snap, e->sync_id) != 0 || fputs("\n---\n", out) < 0 ||
        fputs(e->body, out) < 0 || fputc('\n', out) == EOF) {
        goto done;
    }
    rc = 0;
done:
    free(vector);
    return rc;
}

static const char k_readme_d30[] =
    "This folder is a Fortkeeps export. Import it with: remember import --from-md <folder>\n";

const char *export_readme_d30(void)
{
    return k_readme_d30;
}

int export_readme_write(FILE *out, const ExportDoc *doc)
{
    size_t b = 0;
    const char *sep = "Bins: ";

    if (fputs(k_readme_d30, out) < 0) {
        return -1;
    }
    for (b = 0; b <= (size_t)STORE_BIN_DELETED; b++) {
        if (!doc->bins.in[b]) {
            continue;
        }
        if (fprintf(out, "%s%s %zu", sep, k_bin_names[b], doc->snap->counts[b]) < 0) {
            return -1;
        }
        sep = ", ";
    }
    return fputs(".\n", out) < 0 ? -1 : 0;
}

int export_manifest_write(FILE *out, const ExportDoc *doc)
{
    return (write_header(out, doc) != 0 || fputs("}\n", out) < 0) ? -1 : 0;
}

/* One RFC 4180 cell: quoted, each '"' doubled. NULL is an empty cell. A cell
   that a spreadsheet would run as a formula, or that starts with ', gets one
   leading ', which import strips (decision:plan22-export-csv-formula-escape). */
static int csv_text(FILE *out, const char *s)
{
    if (s == NULL) {
        return 0;
    }
    if (fputc('"', out) == EOF ||
        (s[0] != '\0' && strchr("=+-@\t\r'", s[0]) != NULL && fputc('\'', out) == EOF)) {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if ((*s == '"' && fputc('"', out) == EOF) || fputc(*s, out) == EOF) {
            return -1;
        }
    }
    return fputc('"', out) == EOF ? -1 : 0;
}

typedef int (*CellFn)(FILE *out, const Entry *e, const StoreExportSnapshot *snap);

/* A JSON cell: fn renders it in memory, then it is quoted. */
static int csv_json(FILE *out, CellFn fn, const Entry *e, const StoreExportSnapshot *snap)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *m = open_memstream(&buf, &len);
    int rc = -1;

    if (m == NULL) {
        return -1;
    }
    rc = fn(m, e, snap);
    if (fclose(m) != 0 || rc != 0) {
        free(buf);
        return -1;
    }
    rc = csv_text(out, buf);
    free(buf);
    return rc;
}

static int tags_cell(FILE *out, const Entry *e, const StoreExportSnapshot *snap)
{
    (void)snap;
    return output_json_string_array(out, (const char *const *)e->tags, e->ntags);
}

static int links_cell(FILE *out, const Entry *e, const StoreExportSnapshot *snap)
{
    return write_own_links(out, snap, e->sync_id);
}

static int csv_row(FILE *out, const ExportDoc *doc, const Entry *e)
{
    char *vector = export_vv_sorted(e->version_vector);
    StoreBin bin = store_bin_of(e->deleted_at, e->expires_at, doc->exported_at);
    int rc = -1;

    if (vector == NULL || !actor_json_is_canonical(e->created_by) ||
        !actor_json_is_canonical(e->updated_by)) {
        goto done;
    }
    if (csv_text(out, e->sync_id) != 0 || fputc(',', out) == EOF || csv_text(out, e->key) != 0 ||
        fputc(',', out) == EOF || csv_text(out, e->body) != 0 || fputc(',', out) == EOF ||
        csv_json(out, tags_cell, e, doc->snap) != 0 || fputc(',', out) == EOF ||
        csv_text(out, e->created_by) != 0 || fputc(',', out) == EOF ||
        csv_text(out, e->updated_by) != 0 || fputc(',', out) == EOF ||
        csv_text(out, e->created_at) != 0 || fputc(',', out) == EOF ||
        csv_text(out, e->updated_at) != 0 || fputc(',', out) == EOF ||
        csv_text(out, e->expires_at) != 0 || fputc(',', out) == EOF ||
        csv_text(out, e->deleted_at) != 0 || fputc(',', out) == EOF ||
        csv_text(out, k_bin_names[bin]) != 0 || fputc(',', out) == EOF ||
        csv_text(out, vector) != 0 || fputc(',', out) == EOF ||
        csv_json(out, links_cell, e, doc->snap) != 0 || fputs("\r\n", out) < 0) {
        goto done;
    }
    rc = 0;
done:
    free(vector);
    return rc;
}

int export_csv_write(FILE *out, const ExportDoc *doc)
{
    size_t i = 0;

    if (fputs("\xef\xbb\xbf"
              "id,key,body,tags,created_by,updated_by,created_at,updated_at,expires_at,"
              "deleted_at,bin,version_vector,links\r\n",
              out) < 0) {
        return -1;
    }
    for (i = 0; i < doc->snap->nentries; i++) {
        if (csv_row(out, doc, &doc->snap->entries[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
