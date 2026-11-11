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

/* Copy the JSON object that is the value of "key" near the start of src. */
static char *extract_top_object(const char *src, size_t len, const char *key, size_t *out_len)
{
    char needle[64];
    const char *hit = NULL;
    const char *p = NULL;
    const char *end = NULL;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    bool found = false;
    size_t n = 0;
    size_t need = 0;
    int printed = 0;

    *out_len = 0U;
    printed = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (printed < 0 || (size_t)printed >= sizeof(needle) || src == NULL) {
        return NULL;
    }
    need = (size_t)printed;
    end = src + len;
    for (hit = src; hit + need <= end; hit++) {
        if (memcmp(hit, needle, need) != 0) {
            continue;
        }
        p = hit + need;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            p++;
        }
        if (p >= end || *p != ':') {
            continue;
        }
        p++;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            p++;
        }
        if (p < end && *p == '{') {
            found = true;
            break;
        }
    }
    if (!found || p >= end || *p != '{') {
        return NULL;
    }
    for (n = 0; p + n < end; n++) {
        char c = p[n];

        if (in_str) {
            if (esc) {
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }
        if (c == '"') {
            in_str = true;
            continue;
        }
        if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                n++;
                *out_len = n;
                return dup_bytes(p, n);
            }
        }
    }
    return NULL;
}

static bool known_export_root(const char *key)
{
    return strcmp(key, "format") == 0 || strcmp(key, "version") == 0 ||
           strcmp(key, "brain_id") == 0 || strcmp(key, "exported_at") == 0 ||
           strcmp(key, "engine") == 0 || strcmp(key, "schema") == 0 || strcmp(key, "bins") == 0 ||
           strcmp(key, "entries") == 0 || strcmp(key, "links") == 0 || strcmp(key, "settings") == 0;
}

static bool known_entry_field(const char *key)
{
    return strcmp(key, "id") == 0 || strcmp(key, "key") == 0 || strcmp(key, "body") == 0 ||
           strcmp(key, "tags") == 0 || strcmp(key, "created_by") == 0 ||
           strcmp(key, "updated_by") == 0 || strcmp(key, "created_at") == 0 ||
           strcmp(key, "updated_at") == 0 || strcmp(key, "expires_at") == 0 ||
           strcmp(key, "deleted_at") == 0 || strcmp(key, "bin") == 0 ||
           strcmp(key, "version_vector") == 0;
}

static bool known_link_field(const char *key)
{
    return strcmp(key, "from") == 0 || strcmp(key, "to") == 0 || strcmp(key, "kind") == 0 ||
           strcmp(key, "created_at") == 0 || strcmp(key, "updated_at") == 0 ||
           strcmp(key, "deleted_at") == 0 || strcmp(key, "id") == 0 ||
           strcmp(key, "version_vector") == 0;
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

static void memory_free(ImportMemory *m)
{
    size_t i = 0;

    if (m == NULL) {
        return;
    }
    free(m->id);
    free(m->key);
    free(m->body);
    for (i = 0; i < m->ntags; i++) {
        free(m->tags[i]);
    }
    free(m->tags);
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
    int need = snprintf(NULL, 0,
                        "\"%s\" is remember-export version %lld; this remember reads version 1.",
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

/* Compact actor JSON from a {kind,id} object. NULL if absent or not an object. */
static char *actor_from_value(const JsonValue *v, bool *invalid)
{
    const JsonValue *kind_v = NULL;
    const JsonValue *id_v = NULL;
    Actor actor = {.kind = NULL, .id = NULL};
    char buf[ACTOR_JSON_CAP];
    char *kind = NULL;
    char *id = NULL;

    *invalid = false;
    if (v == NULL || v->type == JSON_NULL) {
        return NULL;
    }
    if (v->type != JSON_OBJECT) {
        *invalid = true;
        return NULL;
    }
    kind_v = object_get(v, "kind");
    id_v = object_get(v, "id");
    if (kind_v == NULL || kind_v->type != JSON_STRING) {
        *invalid = true;
        return NULL;
    }
    kind = json_string_dup(kind_v);
    if (kind == NULL) {
        *invalid = true;
        return NULL;
    }
    if (id_v != NULL && id_v->type == JSON_STRING) {
        id = json_string_dup(id_v);
        if (id == NULL) {
            free(kind);
            *invalid = true;
            return NULL;
        }
    } else if (id_v != NULL && id_v->type != JSON_NULL) {
        free(kind);
        *invalid = true;
        return NULL;
    }
    actor.kind = kind;
    actor.id = id;
    if (actor_json(actor, buf, sizeof(buf)) != ACTOR_OK) {
        free(kind);
        free(id);
        *invalid = true;
        return NULL;
    }
    free(kind);
    free(id);
    return dup_cstr(buf);
}

/* Compact VV object text; NULL if absent. Sets *invalid on bad shape. */
static char *vv_from_value(const JsonValue *v, bool *invalid)
{
    size_t i = 0;
    size_t cap = 64;
    size_t n = 0;
    char *buf = NULL;
    bool first = true;

    *invalid = false;
    if (v == NULL || v->type == JSON_NULL) {
        return NULL;
    }
    if (v->type != JSON_OBJECT) {
        *invalid = true;
        return NULL;
    }
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        *invalid = true;
        return NULL;
    }
    buf[n++] = '{';
    for (i = 0; i < v->u.object.n; i++) {
        const JsonMember *m = &v->u.object.members[i];
        char id_buf[37];
        const char *num = NULL;
        size_t need = 0;

        if (m->key == NULL || !import_uuid_normalize(m->key, id_buf) || m->value == NULL ||
            m->value->type != JSON_NUMBER || m->value->u.number.bytes == NULL) {
            free(buf);
            *invalid = true;
            return NULL;
        }
        num = m->value->u.number.bytes;
        need = n + strlen(id_buf) + strlen(num) + 8U;
        if (need >= cap) {
            size_t next = cap * 2U;
            char *grown = NULL;

            while (next <= need) {
                next *= 2U;
            }
            grown = (char *)realloc(buf, next);
            if (grown == NULL) {
                free(buf);
                *invalid = true;
                return NULL;
            }
            buf = grown;
            cap = next;
        }
        if (!first) {
            buf[n++] = ',';
        }
        first = false;
        buf[n++] = '"';
        memcpy(buf + n, id_buf, 36U);
        n += 36U;
        buf[n++] = '"';
        buf[n++] = ':';
        memcpy(buf + n, num, strlen(num));
        n += strlen(num);
    }
    buf[n++] = '}';
    buf[n] = '\0';
    return buf;
}

static bool tags_from_value(const JsonValue *v, char ***out_tags, size_t *out_n, bool *invalid)
{
    size_t i = 0;
    char **tags = NULL;

    *out_tags = NULL;
    *out_n = 0;
    *invalid = false;
    if (v == NULL || v->type == JSON_NULL) {
        return true;
    }
    if (v->type != JSON_ARRAY) {
        *invalid = true;
        return false;
    }
    if (v->u.array.n == 0U) {
        return true;
    }
    tags = (char **)calloc(v->u.array.n, sizeof(char *));
    if (tags == NULL) {
        *invalid = true;
        return false;
    }
    for (i = 0; i < v->u.array.n; i++) {
        const JsonValue *item = v->u.array.items[i];

        if (item == NULL || item->type != JSON_STRING) {
            size_t j = 0;

            for (j = 0; j < i; j++) {
                free(tags[j]);
            }
            free(tags);
            *invalid = true;
            return false;
        }
        tags[i] = json_string_dup(item);
        if (tags[i] == NULL) {
            size_t j = 0;

            for (j = 0; j < i; j++) {
                free(tags[j]);
            }
            free(tags);
            *invalid = true;
            return false;
        }
    }
    *out_tags = tags;
    *out_n = v->u.array.n;
    return true;
}

static bool memory_from_object(const JsonValue *obj, const char *file, ImportMemory *out)
{
    bool invalid = false;
    const JsonValue *v = NULL;

    memset(out, 0, sizeof(*out));
    if (obj == NULL || obj->type != JSON_OBJECT) {
        out->bad = true;
        out->bad_message = "entry is not an object";
        out->line = (obj != NULL) ? obj->line : 1;
        out->file = dup_cstr(file);
        return out->file != NULL;
    }
    out->line = obj->line;
    out->file = dup_cstr(file);
    if (out->file == NULL) {
        return false;
    }
    v = object_get(obj, "id");
    if (v != NULL && v->type != JSON_NULL) {
        out->id = json_string_dup(v);
        if (out->id == NULL && v->type == JSON_STRING) {
            return false;
        }
        if (v->type != JSON_STRING) {
            out->bad = true;
            out->bad_message = "id is not a UUID";
            return true;
        }
    }
    v = object_get(obj, "key");
    if (v != NULL && v->type != JSON_NULL) {
        out->key = json_string_dup(v);
        if (out->key == NULL && v->type == JSON_STRING) {
            return false;
        }
        if (v->type != JSON_STRING) {
            out->bad = true;
            out->bad_message = "invalid key or tag";
            return true;
        }
    }
    v = object_get(obj, "body");
    if (v == NULL || v->type == JSON_NULL) {
        out->body = NULL;
    } else if (v->type == JSON_STRING) {
        out->body = json_string_dup(v);
        if (out->body == NULL) {
            return false;
        }
    } else {
        out->bad = true;
        out->bad_message = "body is missing";
        return true;
    }
    v = object_get(obj, "tags");
    if (!tags_from_value(v, &out->tags, &out->ntags, &invalid)) {
        if (invalid) {
            out->bad = true;
            out->bad_message = "invalid key or tag";
            return true;
        }
        return false;
    }
    v = object_get(obj, "created_by");
    out->created_by = actor_from_value(v, &invalid);
    if (invalid) {
        out->bad = true;
        out->bad_message = "invalid actor";
        return true;
    }
    v = object_get(obj, "created_at");
    if (v != NULL && v->type != JSON_NULL) {
        out->created_at = json_string_dup(v);
        if (out->created_at == NULL && v->type == JSON_STRING) {
            return false;
        }
        if (v->type != JSON_STRING) {
            out->bad = true;
            out->bad_message = "invalid timestamp";
            return true;
        }
    }
    v = object_get(obj, "updated_at");
    if (v != NULL && v->type != JSON_NULL) {
        out->updated_at = json_string_dup(v);
        if (out->updated_at == NULL && v->type == JSON_STRING) {
            return false;
        }
        if (v->type != JSON_STRING) {
            out->bad = true;
            out->bad_message = "invalid timestamp";
            return true;
        }
    }
    v = object_get(obj, "expires_at");
    if (v != NULL && v->type != JSON_NULL) {
        out->expires_at = json_string_dup(v);
        if (out->expires_at == NULL && v->type == JSON_STRING) {
            return false;
        }
        if (v->type != JSON_STRING) {
            out->bad = true;
            out->bad_message = "invalid timestamp";
            return true;
        }
    }
    v = object_get(obj, "deleted_at");
    if (v != NULL && v->type != JSON_NULL) {
        out->deleted_at = json_string_dup(v);
        if (out->deleted_at == NULL && v->type == JSON_STRING) {
            return false;
        }
        if (v->type != JSON_STRING) {
            out->bad = true;
            out->bad_message = "invalid timestamp";
            return true;
        }
    }
    v = object_get(obj, "version_vector");
    out->version_vector = vv_from_value(v, &invalid);
    if (invalid) {
        out->bad = true;
        out->bad_message = "invalid version_vector";
        return true;
    }
    return true;
}

static bool stamp_from_value(const JsonValue *v, char **out, bool *invalid)
{
    *out = NULL;
    *invalid = false;
    if (v == NULL || v->type == JSON_NULL) {
        return true;
    }
    if (v->type != JSON_STRING) {
        *invalid = true;
        return false;
    }
    *out = json_string_dup(v);
    if (*out == NULL) {
        return false;
    }
    return true;
}

static bool link_from_object(const JsonValue *obj, const char *file, ImportLink *out)
{
    const JsonValue *v = NULL;
    bool invalid = false;

    memset(out, 0, sizeof(*out));
    if (obj == NULL || obj->type != JSON_OBJECT) {
        out->bad = true;
        out->bad_message = "invalid link";
        out->line = (obj != NULL) ? obj->line : 1;
        out->file = dup_cstr(file);
        return out->file != NULL;
    }
    out->line = obj->line;
    out->file = dup_cstr(file);
    if (out->file == NULL) {
        return false;
    }
    v = object_get(obj, "from");
    if (v != NULL && v->type == JSON_STRING) {
        out->from = json_string_dup(v);
        if (out->from == NULL) {
            return false;
        }
    } else if (v != NULL && v->type != JSON_NULL) {
        out->bad = true;
        out->bad_message = "invalid link";
        return true;
    }
    v = object_get(obj, "to");
    if (v != NULL && v->type == JSON_STRING) {
        out->to = json_string_dup(v);
        if (out->to == NULL) {
            return false;
        }
    } else if (v != NULL && v->type != JSON_NULL) {
        out->bad = true;
        out->bad_message = "invalid link";
        return true;
    }
    v = object_get(obj, "kind");
    if (v != NULL && v->type == JSON_STRING) {
        out->kind = json_string_dup(v);
        if (out->kind == NULL) {
            return false;
        }
    } else if (v != NULL && v->type != JSON_NULL) {
        out->bad = true;
        out->bad_message = "invalid link";
        return true;
    }
    if (!stamp_from_value(object_get(obj, "created_at"), &out->created_at, &invalid) ||
        !stamp_from_value(object_get(obj, "updated_at"), &out->updated_at, &invalid) ||
        !stamp_from_value(object_get(obj, "deleted_at"), &out->deleted_at, &invalid)) {
        if (invalid) {
            out->bad = true;
            out->bad_message = "invalid timestamp";
            return true;
        }
        return false;
    }
    return true;
}

int import_read_json(const char *path, ImportDoc *doc, int *sys_errno)
{
    char *bytes = NULL;
    size_t len = 0;
    JsonValue *root = NULL;
    JsonReadStatus jst = JSONREAD_OK;
    const JsonValue *v = NULL;
    const char *base = NULL;
    size_t i = 0;

    if (doc == NULL || path == NULL || sys_errno == NULL) {
        if (sys_errno != NULL) {
            *sys_errno = EINVAL;
        }
        return -1;
    }
    memset(doc, 0, sizeof(*doc));
    if (fileio_read_all(path, &bytes, &len, sys_errno) != 0) {
        return -1;
    }
    base = basename_of(path);
    jst = jsonread_parse(bytes, len, &root);
    if (jst != JSONREAD_OK) {
        free(bytes);
        set_parse(doc, msg_not_export(base), NULL);
        return 0;
    }
    if (root->type != JSON_OBJECT) {
        free(bytes);
        jsonread_free(root);
        set_parse(doc, msg_not_export(base), NULL);
        return 0;
    }
    v = object_get(root, "format");
    if (v == NULL || v->type != JSON_STRING || v->u.string.bytes == NULL ||
        strcmp(v->u.string.bytes, "remember-export") != 0) {
        free(bytes);
        jsonread_free(root);
        set_parse(doc, msg_not_export(base), NULL);
        return 0;
    }
    v = object_get(root, "version");
    if (v == NULL || v->type != JSON_NUMBER || v->u.number.bytes == NULL) {
        free(bytes);
        jsonread_free(root);
        set_parse(doc, msg_not_export(base), NULL);
        return 0;
    }
    {
        char *end = NULL;
        long long ver = strtoll(v->u.number.bytes, &end, 10);

        if (end == v->u.number.bytes || *end != '\0') {
            free(bytes);
            jsonread_free(root);
            set_parse(doc, msg_not_export(base), NULL);
            return 0;
        }
        doc->version = ver;
        if (ver > 1) {
            doc->version_too_new = true;
            set_parse(doc, msg_version_too_new(base, ver),
                      dup_cstr("Update remember, then try again."));
            free(bytes);
            jsonread_free(root);
            return 0;
        }
        if (ver != 1) {
            free(bytes);
            jsonread_free(root);
            set_parse(doc, msg_not_export(base), NULL);
            return 0;
        }
    }
    v = object_get(root, "settings");
    if (v != NULL && v->type != JSON_OBJECT) {
        free(bytes);
        jsonread_free(root);
        /* A present settings member that is not a settings document (ST-8.3a). */
        set_parse(doc, dup_cstr("\"settings\" isn't a settings document."),
                  dup_cstr("Fix the settings object, then try again."));
        return 0;
    }
    if (v != NULL && v->type == JSON_OBJECT) {
        doc->settings_doc = extract_top_object(bytes, len, "settings", &doc->settings_len);
        if (doc->settings_doc == NULL) {
            free(bytes);
            jsonread_free(root);
            set_parse(doc, dup_cstr("\"settings\" isn't a settings document."),
                      dup_cstr("Fix the settings object, then try again."));
            return 0;
        }
    }
    free(bytes);
    v = object_get(root, "brain_id");
    if (v != NULL && v->type == JSON_STRING) {
        char id_buf[37];

        if (import_uuid_normalize(v->u.string.bytes, id_buf)) {
            doc->brain_id = dup_cstr(id_buf);
            if (doc->brain_id == NULL) {
                jsonread_free(root);
                *sys_errno = ENOMEM;
                return -1;
            }
        }
    }
    v = object_get(root, "entries");
    if (v != NULL && v->type == JSON_ARRAY && v->u.array.n > 0U) {
        doc->entries = (ImportMemory *)calloc(v->u.array.n, sizeof(ImportMemory));
        if (doc->entries == NULL) {
            jsonread_free(root);
            *sys_errno = ENOMEM;
            return -1;
        }
        doc->nentries = v->u.array.n;
        for (i = 0; i < v->u.array.n; i++) {
            if (!memory_from_object(v->u.array.items[i], base, &doc->entries[i])) {
                jsonread_free(root);
                import_doc_free(doc);
                *sys_errno = ENOMEM;
                return -1;
            }
        }
    }
    v = object_get(root, "links");
    if (v != NULL && v->type == JSON_ARRAY && v->u.array.n > 0U) {
        doc->links = (ImportLink *)calloc(v->u.array.n, sizeof(ImportLink));
        if (doc->links == NULL) {
            jsonread_free(root);
            import_doc_free(doc);
            *sys_errno = ENOMEM;
            return -1;
        }
        doc->nlinks = v->u.array.n;
        for (i = 0; i < v->u.array.n; i++) {
            if (!link_from_object(v->u.array.items[i], base, &doc->links[i])) {
                jsonread_free(root);
                import_doc_free(doc);
                *sys_errno = ENOMEM;
                return -1;
            }
        }
    }
    /* First unknown member wins (JE-4.1 / C-35); sync_id and source are unknown. */
    doc->unknown_field = first_unknown_in_object(root, known_export_root);
    if (doc->unknown_field == NULL) {
        v = object_get(root, "entries");
        if (v != NULL && v->type == JSON_ARRAY) {
            for (i = 0; i < v->u.array.n && doc->unknown_field == NULL; i++) {
                doc->unknown_field =
                    first_unknown_in_object(v->u.array.items[i], known_entry_field);
            }
        }
    }
    if (doc->unknown_field == NULL) {
        v = object_get(root, "links");
        if (v != NULL && v->type == JSON_ARRAY) {
            for (i = 0; i < v->u.array.n && doc->unknown_field == NULL; i++) {
                doc->unknown_field =
                    first_unknown_in_object(v->u.array.items[i], known_link_field);
            }
        }
    }
    jsonread_free(root);
    import_validate_memories(doc->entries, doc->nentries);
    import_validate_links(doc->links, doc->nlinks);
    return 0;
}
