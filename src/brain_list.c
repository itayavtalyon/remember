#include "brain_list.h"

#include "fileio.h"
#include "jsonread.h"
#include "normalize.h"
#include "output.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h"

/* This module has its own fault counter. The shared one counts the allocations of the settings
   fault sweeps, and the read of brains.json at every open must not use up one of them. */
#undef malloc
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_alloc_after = -1;

void brain_list_test_fail_alloc_after(int n)
{
    g_fail_alloc_after = n;
}

/* An allocator wrapper on purpose; see test_alloc.c for the two pragmas. */
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wallocator-wrappers"
#endif
static void *list_malloc(size_t n)
{
    if (test_countdown_fires(&g_fail_alloc_after)) {
        return NULL;
    }
    return malloc(n);
}
#else
#define list_malloc malloc
#endif

/* lock_file's failures. */
enum { LOCK_IO = -1, LOCK_BUSY = -2 };

static const char k_lock_suffix[] = ".lock";
static const char k_tmp_suffix[] = ".tmp";

/* Every list in memory has one free slot after its last row, so a row can be added. */

static void row_clear(BrainRow *row)
{
    free(row->path);
    free(row->brain_id);
    free(row->added_at);
    memset(row, 0, sizeof(*row));
}

static void doc_clear(BrainList *doc)
{
    size_t i = 0;

    for (i = 0; i < doc->count; i++) {
        row_clear(&doc->rows[i]);
    }
    free(doc->rows);
    free(doc->unknown_field);
    memset(doc, 0, sizeof(*doc));
}

void brain_list_free(BrainList *list)
{
    if (list != NULL) {
        doc_clear(list);
    }
}

/* n zeroed rows. Every allocation here is a malloc, so the test hook reaches it. */
static BrainRow *rows_alloc(size_t n)
{
    BrainRow *rows = (BrainRow *)list_malloc(n * sizeof(BrainRow));

    if (rows != NULL) {
        memset(rows, 0, n * sizeof(BrainRow));
    }
    return rows;
}

static char *dup_text(const char *text)
{
    size_t n = strlen(text);
    char *out = (char *)list_malloc(n + 1U);

    if (out != NULL) {
        memcpy(out, text, n + 1U);
    }
    return out;
}

/* The one member named key. A key given twice is NULL, like a missing one: first-wins here and
   last-wins in most other readers would read two different files (plan 25 X3). */
static const JsonValue *member(const JsonValue *obj, const char *key)
{
    const JsonValue *found = NULL;
    size_t i = 0;

    for (i = 0; i < obj->u.object.n; i++) {
        if (strcmp(obj->u.object.members[i].key, key) == 0) {
            if (found != NULL) {
                return NULL;
            }
            found = obj->u.object.members[i].value;
        }
    }
    return found;
}

/* jsonread refuses a NUL byte and \u0000, so a string here holds no NUL and C sees all of it.
   A missing or wrong-typed member is unreadable; no memory is IO. */
static BrainListCode take_string(const JsonValue *obj, const char *key, char **out)
{
    const JsonValue *value = member(obj, key);

    if (value == NULL || value->type != JSON_STRING) {
        return BRAIN_LIST_UNREADABLE;
    }
    *out = dup_text(value->u.string.bytes);
    return *out != NULL ? BRAIN_LIST_UNCHANGED : BRAIN_LIST_IO;
}

static BrainListCode take_row(const JsonValue *row, BrainRow *out)
{
    BrainListCode code = row->type == JSON_OBJECT ? BRAIN_LIST_UNCHANGED : BRAIN_LIST_UNREADABLE;

    if (code == BRAIN_LIST_UNCHANGED) {
        code = take_string(row, "path", &out->path);
    }
    if (code == BRAIN_LIST_UNCHANGED) {
        code = take_string(row, "brain_id", &out->brain_id);
    }
    if (code == BRAIN_LIST_UNCHANGED) {
        code = take_string(row, "added_at", &out->added_at);
    }
    return code;
}

static bool is_the_document(const JsonValue *root)
{
    const JsonValue *format = NULL;
    const JsonValue *version = NULL;
    const JsonValue *brains = NULL;

    if (root->type != JSON_OBJECT) {
        return false;
    }
    format = member(root, "format");
    version = member(root, "version");
    brains = member(root, "brains");
    if (format == NULL || format->type != JSON_STRING ||
        strcmp(format->u.string.bytes, "remember-brains") != 0) {
        return false;
    }
    if (version == NULL || version->type != JSON_NUMBER ||
        strcmp(version->u.number.bytes, "1") != 0) {
        return false;
    }
    if (brains == NULL || brains->type != JSON_ARRAY) {
        return false;
    }
    return true;
}

enum { KNOWN_KEYS = 3 };
static const char *const k_doc_keys[KNOWN_KEYS] = {"format", "version", "brains"};
static const char *const k_row_keys[KNOWN_KEYS] = {"path", "brain_id", "added_at"};

/* The first member of obj that is not one of known, or NULL. */
static const char *first_unknown(const JsonValue *obj, const char *const *known)
{
    size_t i = 0;

    for (i = 0; i < obj->u.object.n; i++) {
        const char *key = obj->u.object.members[i].key;

        if (strcmp(key, known[0]) != 0 && strcmp(key, known[1]) != 0 &&
            strcmp(key, known[2]) != 0) {
            return key;
        }
    }
    return NULL;
}

/* The first member, root then rows in file order, that the reader does not take
   (decision:2026-11-16-plan26-review-grill G6 B: it is warned, never ignored silently). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the document, then its brains array
static const char *unknown_in_doc(const JsonValue *root, const JsonValue *brains)
{
    const char *key = first_unknown(root, k_doc_keys);
    size_t i = 0;

    for (i = 0; key == NULL && i < brains->u.array.n; i++) {
        key = first_unknown(brains->u.array.items[i], k_row_keys);
    }
    return key;
}

static BrainListCode parse_doc(const char *text, size_t len, BrainList *doc)
{
    const char *unknown = NULL;
    JsonValue *root = NULL;
    const JsonValue *brains = NULL;
    JsonReadStatus read = JSONREAD_OK;
    size_t i = 0;

    memset(doc, 0, sizeof(*doc));
    read = jsonread_parse(text, len, &root);
    if (read == JSONREAD_ERR_OOM) {
        return BRAIN_LIST_IO;
    }
    if (read != JSONREAD_OK || !is_the_document(root)) {
        jsonread_free(root);
        return BRAIN_LIST_UNREADABLE;
    }
    brains = member(root, "brains");
    doc->rows = rows_alloc(brains->u.array.n + 1U);
    if (doc->rows == NULL) {
        jsonread_free(root);
        return BRAIN_LIST_IO;
    }
    for (i = 0; i < brains->u.array.n; i++) {
        BrainListCode code = take_row(brains->u.array.items[i], &doc->rows[i]);

        if (code != BRAIN_LIST_UNCHANGED) {
            row_clear(&doc->rows[i]);
            jsonread_free(root);
            doc_clear(doc);
            return code;
        }
        doc->count += 1U;
    }
    unknown = unknown_in_doc(root, brains);
    if (unknown != NULL) {
        doc->unknown_field = dup_text(unknown);
    }
    jsonread_free(root);
    if (unknown != NULL && doc->unknown_field == NULL) {
        doc_clear(doc);
        return BRAIN_LIST_IO;
    }
    return BRAIN_LIST_UNCHANGED;
}

static int write_doc(FILE *out, const void *ctx)
{
    if (fputs("{\"format\":\"remember-brains\",\"version\":1,", out) < 0 ||
        output_brain_rows_json(out, (const BrainList *)ctx) != 0) {
        return -1;
    }
    return fputs("}\n", out) < 0 ? -1 : 0;
}

/* An absent file is an empty list. Anything else that cannot be used is
   unreadable, and nothing is written over it. */
static BrainListCode load_file(const char *json_path, BrainList *doc)
{
    char *bytes = NULL;
    size_t len = 0;
    int sys_errno = 0;
    BrainListCode code = BRAIN_LIST_UNCHANGED;

    memset(doc, 0, sizeof(*doc));
    if (fileio_read_regular(json_path, (size_t)BRAIN_LIST_FILE_MAX, &bytes, &len, &sys_errno) !=
        0) {
        if (sys_errno == ENOENT) {
            doc->rows = rows_alloc(1U);
            return doc->rows != NULL ? BRAIN_LIST_UNCHANGED : BRAIN_LIST_IO;
        }
        return sys_errno == ENOMEM ? BRAIN_LIST_IO : BRAIN_LIST_UNREADABLE;
    }
    code = parse_doc(bytes, len, doc);
    free(bytes);
    return code;
}

/* The temp name is fixed (decision:2026-11-11-brains-json-format). The lock is
   held, so no other writer is using it; a stale one, a symlink included, is
   unlinked, and O_EXCL in fileio_write_new refuses anything that appears. */
static BrainListCode store_file(const char *json_path, const BrainList *doc)
{
    size_t n = strlen(json_path);
    char *tmp = (char *)list_malloc(n + sizeof(k_tmp_suffix));
    int sys_errno = 0;
    BrainListCode code = BRAIN_LIST_IO;

    if (tmp == NULL) {
        return BRAIN_LIST_IO;
    }
    memcpy(tmp, json_path, n);
    memcpy(tmp + n, k_tmp_suffix, sizeof(k_tmp_suffix));
    (void)unlink(tmp);
    if (fileio_write_new(tmp, write_doc, doc, &sys_errno) == 0) {
        if (fileio_rename(tmp, json_path, &sys_errno) == 0) {
            code = BRAIN_LIST_UNCHANGED;
        } else {
            (void)unlink(tmp);
        }
    }
    free(tmp);
    return code;
}

/* The lock on brains.json.lock (fileio_lock_open: advisory, the OS drops it when the holder dies).
   The data folder is made first, because the first brain a machine opens may be one outside it.
   The fd, or LOCK_IO or LOCK_BUSY. */
static int lock_file(const char *json_path, int timeout_ms)
{
    size_t n = strlen(json_path);
    char *lock_path = (char *)list_malloc(n + sizeof(k_lock_suffix));
    int sys_errno = 0;
    int fd = LOCK_IO;

    if (lock_path == NULL) {
        return LOCK_IO;
    }
    memcpy(lock_path, json_path, n);
    memcpy(lock_path + n, k_lock_suffix, sizeof(k_lock_suffix));
    if (fileio_mkdir_parents(lock_path, &sys_errno) == 0) {
        fd = fileio_lock_open(lock_path, timeout_ms);
    }
    free(lock_path);
    if (fd == FILEIO_LOCK_BUSY) {
        return LOCK_BUSY;
    }
    return fd < 0 ? LOCK_IO : fd;
}

static BrainListCode lock_failure(int rc)
{
    return rc == LOCK_BUSY ? BRAIN_LIST_BUSY : BRAIN_LIST_IO;
}

static bool push_row(BrainList *doc, BrainRecordIn in, const char *brain_id)
{
    BrainRow *row = NULL;

    row = &doc->rows[doc->count];
    row->path = dup_text(in.real_path);
    row->brain_id = dup_text(brain_id);
    row->added_at = dup_text(in.now);
    if (row->path == NULL || row->brain_id == NULL || row->added_at == NULL) {
        row_clear(row);
        return false;
    }
    doc->count += 1U;
    return true;
}

static void drop_at(BrainList *doc, size_t index)
{
    row_clear(&doc->rows[index]);
    memmove(&doc->rows[index], &doc->rows[index + 1U],
            (doc->count - index - 1U) * sizeof(BrainRow));
    doc->count -= 1U;
}

/* Replace a heap string with a copy of text; false when memory ran out. */
static bool set_text(char **slot, const char *text)
{
    char *copy = dup_text(text);

    if (copy == NULL) {
        return false;
    }
    free(*slot);
    *slot = copy;
    return true;
}

static BrainListCode apply_clone(BrainList *doc, BrainRecordIn in, size_t path_at,
                                 const BrainProbe *probe, char *minted, size_t minted_cap)
{
    if (probe->mint(probe->ctx, minted, minted_cap) != 0) {
        return BRAIN_LIST_IO;
    }
    if (path_at < doc->count ? !set_text(&doc->rows[path_at].brain_id, minted)
                             : !push_row(doc, in, minted)) {
        return BRAIN_LIST_IO;
    }
    return BRAIN_LIST_CLONED;
}

static BrainListCode apply_record(BrainList *doc, BrainRecordIn in, const BrainProbe *probe,
                                  char *minted, size_t minted_cap)
{
    size_t i = 0;
    size_t path_at = doc->count;
    size_t id_at = doc->count;

    for (i = 0; i < doc->count; i++) {
        if (strcmp(doc->rows[i].path, in.real_path) == 0) {
            path_at = i;
        } else if (strcmp(doc->rows[i].brain_id, in.brain_id) == 0) {
            id_at = i;
        }
    }
    if (id_at < doc->count) {
        int still = probe->still(probe->ctx, doc->rows[id_at].path, in.brain_id);

        if (still < 0) {
            return BRAIN_LIST_UNDECIDED;
        }
        if (still > 0) {
            return apply_clone(doc, in, path_at, probe, minted, minted_cap);
        }
        if (!set_text(&doc->rows[id_at].path, in.real_path)) {
            return BRAIN_LIST_IO;
        }
        if (path_at < doc->count) {
            drop_at(doc, path_at);
        }
        return BRAIN_LIST_MOVED;
    }
    if (path_at < doc->count) {
        if (strcmp(doc->rows[path_at].brain_id, in.brain_id) == 0) {
            return BRAIN_LIST_UNCHANGED;
        }
        if (!set_text(&doc->rows[path_at].brain_id, in.brain_id)) {
            return BRAIN_LIST_IO;
        }
        return BRAIN_LIST_UPDATED;
    }
    if (!push_row(doc, in, in.brain_id)) {
        return BRAIN_LIST_IO;
    }
    return BRAIN_LIST_ADDED;
}

/* load_file leaves out empty on every failure. */
BrainListCode brain_list_read(const char *json_path, BrainList *out)
{
    if (out == NULL || json_path == NULL) {
        return BRAIN_LIST_IO;
    }
    return load_file(json_path, out);
}

/* Hand the first unknown member's name to the caller, who warns it (may be NULL). */
static void pass_unknown(BrainList *doc, char **unknown_field)
{
    if (unknown_field != NULL) {
        *unknown_field = doc->unknown_field;
        doc->unknown_field = NULL;
    }
}

static bool changes_the_file(BrainListCode code)
{
    return (code == BRAIN_LIST_ADDED || code == BRAIN_LIST_MOVED || code == BRAIN_LIST_CLONED ||
            code == BRAIN_LIST_UPDATED) != 0;
}

BrainListCode brain_list_record(const char *json_path, BrainRecordIn in, const BrainProbe *probe,
                                BrainRecordOut out)
{
    BrainList doc;
    BrainListCode code = BRAIN_LIST_IO;
    int lock_fd = -1;

    if (out.unknown_field != NULL) {
        *out.unknown_field = NULL;
    }
    if (json_path == NULL || in.real_path == NULL || in.brain_id == NULL || in.now == NULL ||
        probe == NULL || probe->still == NULL || probe->mint == NULL || out.minted == NULL ||
        out.minted_cap == 0U) {
        return BRAIN_LIST_IO;
    }
    /* A path that is not UTF-8 would make the whole file unreadable for every brain. */
    if (!utf8_is_valid(in.real_path, strlen(in.real_path))) {
        return BRAIN_LIST_IO;
    }
    lock_fd = lock_file(json_path, in.lock_timeout_ms);
    if (lock_fd < 0) {
        return lock_failure(lock_fd);
    }
    code = load_file(json_path, &doc);
    if (code == BRAIN_LIST_UNCHANGED) {
        pass_unknown(&doc, out.unknown_field);
        code = apply_record(&doc, in, probe, out.minted, out.minted_cap);
        if (changes_the_file(code) && store_file(json_path, &doc) != BRAIN_LIST_UNCHANGED) {
            code = BRAIN_LIST_IO;
        }
    }
    doc_clear(&doc);
    (void)close(lock_fd);
    return code;
}

/* Move every row that missing() flags out of doc into gone, in one pass. */
static BrainListCode split_missing(BrainList *doc, BrainMissingFn missing, void *ctx,
                                   BrainList *gone)
{
    size_t keep = 0;
    size_t i = 0;

    gone->rows = rows_alloc(doc->count + 1U);
    if (gone->rows == NULL) {
        return BRAIN_LIST_IO;
    }
    for (i = 0; i < doc->count; i++) {
        if (missing(ctx, doc->rows[i].path)) {
            gone->rows[gone->count] = doc->rows[i];
            gone->count += 1U;
        } else {
            doc->rows[keep] = doc->rows[i];
            keep += 1U;
        }
    }
    doc->count = keep;
    return BRAIN_LIST_UNCHANGED;
}

BrainListCode brain_list_prune(const char *json_path, BrainMissingFn missing, void *ctx,
                               int lock_timeout_ms, BrainList *removed, char **unknown_field)
{
    BrainList doc;
    BrainList gone = {0};
    BrainListCode code = BRAIN_LIST_IO;
    int lock_fd = -1;

    if (removed != NULL) {
        *removed = (BrainList){0};
    }
    if (unknown_field != NULL) {
        *unknown_field = NULL;
    }
    if (json_path == NULL || missing == NULL) {
        return BRAIN_LIST_IO;
    }
    lock_fd = lock_file(json_path, lock_timeout_ms);
    if (lock_fd < 0) {
        return lock_failure(lock_fd);
    }
    code = load_file(json_path, &doc);
    if (code == BRAIN_LIST_UNCHANGED) {
        pass_unknown(&doc, unknown_field);
        code = split_missing(&doc, missing, ctx, &gone);
    }
    if (code == BRAIN_LIST_UNCHANGED && gone.count > 0U) {
        code =
            store_file(json_path, &doc) == BRAIN_LIST_UNCHANGED ? BRAIN_LIST_PRUNED : BRAIN_LIST_IO;
    }
    if (code == BRAIN_LIST_PRUNED && removed != NULL) {
        *removed = (BrainList){.rows = gone.rows, .count = gone.count};
        gone.rows = NULL;
        gone.count = 0U;
    }
    doc_clear(&gone);
    doc_clear(&doc);
    (void)close(lock_fd);
    return code;
}
