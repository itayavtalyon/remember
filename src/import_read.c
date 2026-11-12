#include "import_read.h"

#include "fileio.h"
#include "import_validate.h"
#include "jsonread.h"
#include "normalize.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_alloc.h" /* IWYU pragma: keep */

enum { DECIMAL_BASE = 10 };

static char *dup_bytes(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1U);

    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char *dup_cstr(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    return dup_bytes(s, strlen(s));
}

static const char *basename_of(const char *path)
{
    const char *slash = NULL;
    const char *p = path;

    if (path == NULL) {
        return "";
    }
    for (; *p != '\0'; p++) {
        if (*p == '/') {
            slash = p;
        }
    }
    return (slash != NULL) ? (slash + 1) : path;
}

static bool name_in(const char *key, const char *const *names)
{
    for (; *names != NULL; names++) {
        if (strcmp(key, *names) == 0) {
            return true;
        }
    }
    return false;
}

static bool known_export_root(const char *key)
{
    static const char *const names[] = {"format", "version",  "brain_id", "exported_at",
                                        "engine", "schema",   "bins",     "entries",
                                        "links",  "settings", NULL};
    return name_in(key, names);
}

static bool known_entry_field(const char *key)
{
    static const char *const names[] = {"id",         "key",        "body",       "tags",
                                        "created_by", "updated_by", "created_at", "updated_at",
                                        "expires_at", "deleted_at", "bin",        "version_vector",
                                        NULL};
    return name_in(key, names);
}

static bool known_link_field(const char *key)
{
    static const char *const names[] = {"from",       "to",         "kind", "created_at",
                                        "updated_at", "deleted_at", "id",   "version_vector",
                                        NULL};
    return name_in(key, names);
}

static char *first_unknown_in_object(const JsonValue *obj, bool (*known)(const char *))
{
    size_t i = 0;

    if (obj == NULL || obj->type != JSON_OBJECT || known == NULL) {
        return NULL;
    }
    for (i = 0; i < obj->u.object.n; i++) {
        const char *key = obj->u.object.members[i].key;

        if (key != NULL && !known(key)) {
            return dup_cstr(key);
        }
    }
    return NULL;
}

static void free_tags(char **tags, size_t n)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        free(tags[i]);
    }
    free((void *)tags);
}

static void memory_free(ImportMemory *m)
{
    if (m == NULL) {
        return;
    }
    free(m->id);
    free(m->key);
    free(m->body);
    free_tags(m->tags, m->ntags);
    free(m->created_by);
    free(m->created_at);
    free(m->updated_at);
    free(m->expires_at);
    free(m->deleted_at);
    free(m->version_vector);
    free(m->file);
    memset(m, 0, sizeof(*m));
}

static void link_free(ImportLink *l)
{
    if (l == NULL) {
        return;
    }
    free(l->from);
    free(l->to);
    free(l->kind);
    free(l->created_at);
    free(l->updated_at);
    free(l->deleted_at);
    free(l->file);
    memset(l, 0, sizeof(*l));
}

void import_doc_free(ImportDoc *doc)
{
    size_t i = 0;

    if (doc == NULL) {
        return;
    }
    free(doc->brain_id);
    for (i = 0; i < doc->nentries; i++) {
        memory_free(&doc->entries[i]);
    }
    free(doc->entries);
    for (i = 0; i < doc->nlinks; i++) {
        link_free(&doc->links[i]);
    }
    free(doc->links);
    free(doc->settings_doc);
    free(doc->unknown_field);
    free(doc->parse_message);
    free(doc->parse_fix);
    memset(doc, 0, sizeof(*doc));
}

static char *msg_not_export(const char *base)
{
    char *out = NULL;
    int need = snprintf(NULL, 0, "\"%s\" isn't a remember export.", base);

    if (need < 0) {
        return NULL;
    }
    out = (char *)malloc((size_t)need + 1U);
    if (out == NULL) {
        return NULL;
    }
    (void)snprintf(out, (size_t)need + 1U, "\"%s\" isn't a remember export.", base);
    return out;
}

static char *msg_version_too_new(const char *base, long long ver)
{
    char *out = NULL;
    int need =
        snprintf(NULL, 0, "\"%s\" is remember-export version %lld; this remember reads version 1.",
                 base, ver);

    if (need < 0) {
        return NULL;
    }
    out = (char *)malloc((size_t)need + 1U);
    if (out == NULL) {
        return NULL;
    }
    (void)snprintf(out, (size_t)need + 1U,
                   "\"%s\" is remember-export version %lld; this remember reads version 1.", base,
                   ver);
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the message, then its fix
static void set_parse(ImportDoc *doc, char *message, char *fix)
{
    free(doc->parse_message);
    free(doc->parse_fix);
    doc->parse_message = message;
    doc->parse_fix = fix;
}

static const JsonValue *object_get(const JsonValue *obj, const char *key)
{
    size_t i = 0;

    if (obj == NULL || obj->type != JSON_OBJECT || key == NULL) {
        return NULL;
    }
    for (i = 0; i < obj->u.object.n; i++) {
        if (obj->u.object.members[i].key != NULL &&
            strcmp(obj->u.object.members[i].key, key) == 0) {
            return obj->u.object.members[i].value;
        }
    }
    return NULL;
}

static char *json_string_dup(const JsonValue *v)
{
    if (v == NULL || v->type != JSON_STRING || v->u.string.bytes == NULL) {
        return NULL;
    }
    return dup_bytes(v->u.string.bytes, v->u.string.len);
}

typedef enum { FIELD_OK, FIELD_BAD, FIELD_OOM } FieldStatus;

/* An optional string member: absent or null leaves *out NULL. */
static FieldStatus optional_string(const JsonValue *v, char **out)
{
    *out = NULL;
    if (v == NULL || v->type == JSON_NULL) {
        return FIELD_OK;
    }
    if (v->type != JSON_STRING) {
        return FIELD_BAD;
    }
    *out = json_string_dup(v);
    return (*out == NULL) ? FIELD_OOM : FIELD_OK;
}

/* Compact actor JSON from a {kind,id} object. NULL if absent; *invalid when
   the shape is wrong (or out of memory: a bad record either way). */
static char *actor_from_value(const JsonValue *v, bool *invalid)
{
    char buf[ACTOR_JSON_CAP];
    char *kind = NULL;
    char *id = NULL;
    char *out = NULL;

    *invalid = false;
    if (v == NULL || v->type == JSON_NULL) {
        return NULL;
    }
    if (v->type == JSON_OBJECT && optional_string(object_get(v, "kind"), &kind) == FIELD_OK &&
        kind != NULL && optional_string(object_get(v, "id"), &id) == FIELD_OK &&
        actor_json((Actor){.kind = kind, .id = id}, buf, sizeof(buf)) == ACTOR_OK) {
        out = dup_cstr(buf);
    }
    free(kind);
    free(id);
    if (out == NULL) {
        *invalid = true;
    }
    return out;
}

/* Compact VV object text; NULL if absent. Sets *invalid on bad shape. */
static char *vv_from_value(const JsonValue *v, bool *invalid)
{
    char buf[REMEMBER_VV_TEXT_MAX];
    size_t n = 1;
    size_t i = 0;

    *invalid = true;
    if (v == NULL || v->type == JSON_NULL) {
        *invalid = false;
        return NULL;
    }
    if (v->type != JSON_OBJECT) {
        return NULL;
    }
    buf[0] = '{';
    for (i = 0; i < v->u.object.n; i++) {
        const JsonMember *m = &v->u.object.members[i];
        char id_buf[IMPORT_UUID_BUF];
        int wrote = 0;

        /* A counter is a JSON integer greater than 0 (plan 23); the canonical
           check in validate rejects fractions, exponents and signs. */
        if (m->key == NULL || !import_uuid_normalize(m->key, id_buf) || m->value == NULL ||
            m->value->type != JSON_NUMBER || strcmp(m->value->u.number.bytes, "0") == 0) {
            return NULL;
        }
        wrote = snprintf(buf + n, sizeof(buf) - n, "%s\"%s\":%s", (i > 0U) ? "," : "", id_buf,
                         m->value->u.number.bytes);
        if (wrote < 0 || (size_t)wrote >= sizeof(buf) - n) {
            return NULL; /* wider than any merge can hold: a bad record (settings A4 rule) */
        }
        n += (size_t)wrote;
    }
    if (n + 1U >= sizeof(buf)) {
        return NULL;
    }
    buf[n++] = '}';
    buf[n] = '\0';
    {
        char *out = dup_bytes(buf, n);

        *invalid = (out == NULL); /* out of memory: a bad record, as before */
        return out;
    }
}

/* Tags as an array of strings. false with *invalid on a wrong shape (or out of
   memory: a bad record either way). */
static bool tags_from_value(const JsonValue *v, char ***out_tags, size_t *out_n, bool *invalid)
{
    size_t n = 0;
    char **tags = NULL;

    *out_tags = NULL;
    *out_n = 0;
    *invalid = true;
    if (v == NULL || v->type == JSON_NULL) {
        *invalid = false;
        return true;
    }
    if (v->type != JSON_ARRAY) {
        return false;
    }
    n = v->u.array.n;
    tags = (char **)calloc((n > 0U) ? n : 1U, sizeof(char *));
    if (tags == NULL) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        tags[i] = json_string_dup(v->u.array.items[i]);
        if (tags[i] == NULL) {
            free_tags(tags, i);
            return false;
        }
    }
    *out_tags = tags;
    *out_n = n;
    *invalid = false;
    return true;
}

static bool array_or_absent(const JsonValue *v)
{
    if (v == NULL || v->type == JSON_NULL || v->type == JSON_ARRAY) {
        return true;
    }
    return false;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) qsort's signature
static int cmp_member_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* P8: a member name twice in one entry or link. Sorted, so a hostile object
   with many members costs O(n log n). -1 out of memory. */
static int has_duplicate_member(const JsonValue *obj)
{
    size_t n = obj->u.object.n;
    const char **names = NULL;
    size_t i = 0;
    int dup = 0;

    if (n < 2U) {
        return 0;
    }
    names = (const char **)calloc(n, sizeof(*names));
    if (names == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        names[i] = obj->u.object.members[i].key;
    }
    qsort((void *)names, n, sizeof(*names), cmp_member_name);
    for (i = 1; i < n && dup == 0; i++) {
        dup = strcmp(names[i - 1U], names[i]) == 0;
    }
    free((void *)names);
    return dup;
}

/* Set out bad with "duplicate field" when obj repeats a member. false out of memory. */
static bool check_duplicate_member(const JsonValue *obj, bool *bad, const char **bad_message)
{
    int dup = has_duplicate_member(obj);

    if (dup < 0) {
        return false;
    }
    if (dup > 0) {
        *bad = true;
        *bad_message = "duplicate field";
    }
    return true;
}

/* Mark a memory bad; true so callers can return it. */
static bool memory_bad(ImportMemory *out, const char *message)
{
    out->bad = true;
    out->bad_message = message;
    return true;
}

/* id, key and body. false out of memory; a wrong type marks out bad. */
static bool memory_strings(const JsonValue *obj, ImportMemory *out)
{
    FieldStatus st = optional_string(object_get(obj, "id"), &out->id);

    if (st == FIELD_BAD) {
        return memory_bad(out, "id is not a UUID");
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "key"), &out->key);
        if (st == FIELD_BAD) {
            return memory_bad(out, "invalid key or tag");
        }
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "body"), &out->body);
        if (st == FIELD_BAD) {
            return memory_bad(out, "body is missing");
        }
    }
    return st == FIELD_OK;
}

/* tags, created_by, the timestamps and the version vector. */
static bool memory_rest(const JsonValue *obj, ImportMemory *out)
{
    bool invalid = false;
    FieldStatus st = FIELD_OK;

    if (!tags_from_value(object_get(obj, "tags"), &out->tags, &out->ntags, &invalid)) {
        if (invalid) {
            return memory_bad(out, "invalid key or tag");
        }
        return false;
    }
    out->created_by = actor_from_value(object_get(obj, "created_by"), &invalid);
    if (invalid) {
        return memory_bad(out, "invalid actor");
    }
    st = optional_string(object_get(obj, "created_at"), &out->created_at);
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "updated_at"), &out->updated_at);
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "expires_at"), &out->expires_at);
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "deleted_at"), &out->deleted_at);
    }
    if (st == FIELD_BAD) {
        return memory_bad(out, "invalid timestamp");
    }
    if (st == FIELD_OOM) {
        return false;
    }
    out->version_vector = vv_from_value(object_get(obj, "version_vector"), &invalid);
    if (invalid) {
        return memory_bad(out, "invalid version_vector");
    }
    return true;
}

static bool memory_from_object(const JsonValue *obj, const char *file, ImportMemory *out)
{
    memset(out, 0, sizeof(*out));
    out->line = (obj != NULL) ? obj->line : 1;
    out->file = dup_cstr(file);
    if (out->file == NULL) {
        return false;
    }
    if (obj == NULL || obj->type != JSON_OBJECT) {
        return memory_bad(out, "entry is not an object");
    }
    if (!check_duplicate_member(obj, &out->bad, &out->bad_message)) {
        return false;
    }
    if (out->bad) {
        return true;
    }
    if (!memory_strings(obj, out)) {
        return false;
    }
    if (out->bad) {
        return true;
    }
    return memory_rest(obj, out);
}

static bool link_bad(ImportLink *out, const char *message)
{
    out->bad = true;
    out->bad_message = message;
    return true;
}

static bool link_from_object(const JsonValue *obj, const char *file, ImportLink *out)
{
    FieldStatus st = FIELD_OK;

    memset(out, 0, sizeof(*out));
    out->line = (obj != NULL) ? obj->line : 1;
    out->file = dup_cstr(file);
    if (out->file == NULL) {
        return false;
    }
    if (obj == NULL || obj->type != JSON_OBJECT) {
        return link_bad(out, "invalid link");
    }
    if (!check_duplicate_member(obj, &out->bad, &out->bad_message)) {
        return false;
    }
    if (out->bad) {
        return true;
    }
    st = optional_string(object_get(obj, "from"), &out->from);
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "to"), &out->to);
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "kind"), &out->kind);
    }
    if (st == FIELD_BAD) {
        return link_bad(out, "invalid link");
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "created_at"), &out->created_at);
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "updated_at"), &out->updated_at);
    }
    if (st == FIELD_OK) {
        st = optional_string(object_get(obj, "deleted_at"), &out->deleted_at);
    }
    if (st == FIELD_BAD) {
        return link_bad(out, "invalid timestamp");
    }
    return st == FIELD_OK;
}

/* The read steps below return 1 to go on, 0 when doc->parse_message is set,
   and -1 out of memory. */

static int not_export(ImportDoc *doc, const char *base)
{
    set_parse(doc, msg_not_export(base), NULL);
    return 0;
}

/* "format" and "version" (JE-4.1). */
static int read_version(const JsonValue *root, ImportDoc *doc, const char *base)
{
    const JsonValue *v = object_get(root, "format");
    char *end = NULL;
    long long ver = 0;

    if (v == NULL || v->type != JSON_STRING || strcmp(v->u.string.bytes, "remember-export") != 0) {
        return not_export(doc, base);
    }
    v = object_get(root, "version");
    if (v == NULL || v->type != JSON_NUMBER) {
        return not_export(doc, base);
    }
    ver = strtoll(v->u.number.bytes, &end, DECIMAL_BASE);
    if (end == v->u.number.bytes || *end != '\0') {
        return not_export(doc, base);
    }
    doc->version = ver;
    if (ver > 1) {
        doc->version_too_new = true;
        set_parse(doc, msg_version_too_new(base, ver),
                  dup_cstr("Update remember, then try again."));
        return 0;
    }
    if (ver != 1) {
        return not_export(doc, base);
    }
    return 1;
}

/* Root checks: shape, version, settings bytes, entries and links shape, brain_id. */
static int read_root(const JsonValue *root, const char *bytes, ImportDoc *doc, const char *base)
{
    const JsonValue *v = NULL;
    char id_buf[IMPORT_UUID_BUF];
    int rc = (root->type == JSON_OBJECT) ? read_version(root, doc, base) : not_export(doc, base);

    if (rc != 1) {
        return rc;
    }
    v = object_get(root, "settings");
    if (v != NULL && v->type != JSON_OBJECT) {
        /* A present settings member that is not a settings document (ST-8.3a). */
        set_parse(doc, dup_cstr("\"settings\" isn't a settings document."),
                  dup_cstr("Fix the settings object, then try again."));
        return 0;
    }
    if (v != NULL) {
        /* The root member's own bytes, never a same-named member deeper in the file. */
        doc->settings_doc = dup_bytes(bytes + v->src_at, v->src_len);
        if (doc->settings_doc == NULL) {
            return -1;
        }
        doc->settings_len = v->src_len;
    }
    /* A wrong root shape is not a remember export
       (decision:plan23-review-g4-shape-is-import-parse). Null means absent (D31). */
    if (!array_or_absent(object_get(root, "entries")) ||
        !array_or_absent(object_get(root, "links"))) {
        return not_export(doc, base);
    }
    v = object_get(root, "brain_id");
    if (v == NULL || v->type == JSON_NULL) {
        return 1;
    }
    if (v->type != JSON_STRING || !import_uuid_normalize(v->u.string.bytes, id_buf)) {
        return not_export(doc, base);
    }
    doc->brain_id = dup_cstr(id_buf);
    return (doc->brain_id != NULL) ? 1 : -1;
}

/* The entries and links arrays into doc. */
static int read_records(const JsonValue *root, ImportDoc *doc, const char *base)
{
    const JsonValue *v = object_get(root, "entries");

    if (v != NULL && v->type == JSON_ARRAY && v->u.array.n > 0U) {
        doc->entries = (ImportMemory *)calloc(v->u.array.n, sizeof(ImportMemory));
        if (doc->entries == NULL) {
            return -1;
        }
        doc->nentries = v->u.array.n;
        for (size_t i = 0; i < v->u.array.n; i++) {
            if (!memory_from_object(v->u.array.items[i], base, &doc->entries[i])) {
                return -1;
            }
        }
    }
    v = object_get(root, "links");
    if (v != NULL && v->type == JSON_ARRAY && v->u.array.n > 0U) {
        doc->links = (ImportLink *)calloc(v->u.array.n, sizeof(ImportLink));
        if (doc->links == NULL) {
            return -1;
        }
        doc->nlinks = v->u.array.n;
        for (size_t i = 0; i < v->u.array.n; i++) {
            if (!link_from_object(v->u.array.items[i], base, &doc->links[i])) {
                return -1;
            }
        }
    }
    return 1;
}

/* The first unknown member in an array of objects, or NULL. */
static char *first_unknown_in_array(const JsonValue *arr, bool (*known)(const char *))
{
    size_t i = 0;
    char *name = NULL;

    if (arr == NULL || arr->type != JSON_ARRAY) {
        return NULL;
    }
    for (i = 0; i < arr->u.array.n && name == NULL; i++) {
        name = first_unknown_in_object(arr->u.array.items[i], known);
    }
    return name;
}

/* First unknown member wins (JE-4.1 / C-35); sync_id and source are unknown. */
static void find_unknown_field(const JsonValue *root, ImportDoc *doc)
{
    doc->unknown_field = first_unknown_in_object(root, known_export_root);
    if (doc->unknown_field == NULL) {
        doc->unknown_field = first_unknown_in_array(object_get(root, "entries"), known_entry_field);
    }
    if (doc->unknown_field == NULL) {
        doc->unknown_field = first_unknown_in_array(object_get(root, "links"), known_link_field);
    }
}

int import_read_json(const char *path, ImportDoc *doc, int *sys_errno)
{
    char *bytes = NULL;
    size_t len = 0;
    JsonValue *root = NULL;
    const char *base = NULL;
    JsonReadStatus jst = JSONREAD_OK;
    int rc = 1;

    if (doc == NULL || path == NULL || sys_errno == NULL) {
        return -1;
    }
    memset(doc, 0, sizeof(*doc));
    if (fileio_read_all(path, &bytes, &len, sys_errno) != 0) {
        return -1;
    }
    *sys_errno = ENOMEM; /* every later -1 is out of memory */
    base = basename_of(path);
    jst = jsonread_parse(bytes, len, &root);
    if (jst != JSONREAD_OK) {
        free(bytes);
        if (jst == JSONREAD_ERR_OOM) {
            return -1;
        }
        (void)not_export(doc, base);
        return 0;
    }
    rc = read_root(root, bytes, doc, base);
    free(bytes);
    if (rc == 1) {
        rc = read_records(root, doc, base);
    }
    if (rc == 1) {
        find_unknown_field(root, doc);
        rc = (import_validate_memories(doc->entries, doc->nentries) == 0) ? 1 : -1;
    }
    jsonread_free(root);
    if (rc < 0) {
        import_doc_free(doc);
        return -1;
    }
    if (rc == 1) {
        import_validate_links(doc->links, doc->nlinks);
    }
    return 0;
}
