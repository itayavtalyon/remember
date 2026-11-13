#include "import_validate.h"

#include "normalize.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "test_alloc.h" /* IWYU pragma: keep */

/* qsort order for tags: load_tags uses ORDER BY name, so E7.2 matches sets. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) qsort's signature
static int cmp_tag(const void *a, const void *b)
{
    const char *const *left = (const char *const *)a;
    const char *const *right = (const char *const *)b;

    return strcmp(*left, *right);
}

static bool is_hex(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        return true;
    }
    return false;
}

bool import_uuid_normalize(const char *s, char out[IMPORT_UUID_BUF])
{
    static const int dashes[] = {8, 13, 18, 23};
    size_t i = 0;
    size_t d = 0;

    if (s == NULL || out == NULL) {
        return false;
    }
    for (i = 0; i < (size_t)IMPORT_UUID_LEN; i++) {
        unsigned char c = (unsigned char)s[i];

        if (d < 4U && (int)i == dashes[d]) {
            if (c != '-') {
                return false;
            }
            out[i] = '-';
            d++;
            continue;
        }
        if (!is_hex(c)) {
            return false;
        }
        /* (tolower) calls the function: glibc's recursive macro trips -Weverything. */
        // NOLINTNEXTLINE(readability-redundant-parentheses)
        out[i] = (char)(tolower)(c);
    }
    if (s[IMPORT_UUID_LEN] != '\0') {
        return false;
    }
    out[IMPORT_UUID_LEN] = '\0';
    return true;
}

/* D35: a canonical UUID of either case becomes lowercase. Same length, so in place. */
static bool uuid_lowercase_in_place(char *s)
{
    char out[IMPORT_UUID_BUF];

    if (!import_uuid_normalize(s, out)) {
        return false;
    }
    memcpy(s, out, sizeof(out));
    return true;
}

/* validate_one's out-of-memory result; import_validate_memories turns it into -1. */
static const char k_oom[] = "out of memory";

/* Replace *slot with its normalized text, the text add stores
   (decision:plan23-review-g6-normalize-keys-tags). 1 ok, 0 invalid, -1 out of memory. */
static int token_normalize(char **slot)
{
    char buf[REMEMBER_TOKEN_MAX + 1];
    size_t len = 0;
    char *copy = NULL;

    if (*slot == NULL || normalize_token(*slot, buf, sizeof(buf)) != NORM_OK) {
        return 0;
    }
    if (strcmp(buf, *slot) == 0) {
        return 1;
    }
    len = strlen(buf);
    copy = (char *)malloc(len + 1U);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, buf, len + 1U);
    free(*slot);
    *slot = copy;
    return 1;
}

/* Sorted tags with repeats dropped: "Pad" and "pad" are one tag once normalized. */
static void tags_sort_unique(ImportMemory *m)
{
    size_t keep = 1;
    size_t i = 0;

    if (m->ntags < 2U) {
        return;
    }
    qsort((void *)m->tags, m->ntags, sizeof(*m->tags), cmp_tag);
    for (i = 1; i < m->ntags; i++) {
        if (strcmp(m->tags[keep - 1U], m->tags[i]) == 0) {
            free(m->tags[i]);
        } else {
            m->tags[keep++] = m->tags[i];
        }
    }
    m->ntags = keep;
}

/* Trim the body in place. NULL when it is fine. */
static const char *validate_body(ImportMemory *m)
{
    char *trimmed = NULL;
    size_t tlen = 0;
    NormStatus bst = NORM_OK;

    if (m->body == NULL) {
        return "body is missing";
    }
    bst = body_trim_copy(m->body, strlen(m->body), &trimmed, &tlen);
    if (bst == NORM_ERR_EMPTY) {
        return "body is empty";
    }
    if (bst == NORM_ERR_TOO_LONG) {
        return "body is too long";
    }
    if (bst != NORM_OK) {
        return "body is missing";
    }
    free(m->body);
    m->body = trimmed;
    return NULL;
}

/* Normalize the key and tags in place (decision:plan23-review-g6-normalize-keys-tags). */
static const char *validate_tokens(ImportMemory *m)
{
    size_t i = 0;
    int k = 1;

    if (m->key != NULL) {
        k = token_normalize(&m->key);
    }
    for (i = 0; i < m->ntags && k > 0; i++) {
        k = token_normalize(&m->tags[i]);
    }
    if (k <= 0) {
        return (k < 0) ? k_oom : "invalid key or tag";
    }
    tags_sort_unique(m);
    return NULL;
}

static bool stamp_ok(const char *stamp)
{
    if (stamp == NULL || utc_stamp_is_canonical(stamp)) {
        return true;
    }
    return false;
}

static const char *validate_one(ImportMemory *m)
{
    const char *msg = NULL;

    if (m == NULL) {
        return "entry is not an object";
    }
    if (m->id != NULL && !uuid_lowercase_in_place(m->id)) {
        return "id is not a UUID";
    }
    msg = validate_body(m);
    if (msg == NULL) {
        msg = validate_tokens(m);
    }
    if (msg != NULL) {
        return msg;
    }
    if (!stamp_ok(m->created_at) || !stamp_ok(m->updated_at) || !stamp_ok(m->expires_at) ||
        !stamp_ok(m->deleted_at)) {
        return "invalid timestamp";
    }
    if (m->created_by != NULL && !actor_json_is_canonical(m->created_by)) {
        return "invalid actor";
    }
    if (m->version_vector != NULL && !vv_json_is_canonical(m->version_vector)) {
        return "invalid version_vector";
    }
    return NULL;
}

/* Same id, then file order: the first copy of an id sorts first. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) qsort's signature
static int cmp_id_then_position(const void *a, const void *b)
{
    const ImportMemory *const *left = (const ImportMemory *const *)a;
    const ImportMemory *const *right = (const ImportMemory *const *)b;
    int c = strcmp((*left)->id, (*right)->id);

    if (c != 0) {
        return c;
    }
    return (*left < *right) ? -1 : (*left > *right);
}

int import_validate_memories(ImportMemory *entries, size_t n)
{
    ImportMemory **with_id = NULL;
    size_t nid = 0;
    size_t i = 0;

    for (i = 0; i < n; i++) {
        const char *msg = NULL;

        if (entries[i].bad) {
            continue;
        }
        msg = validate_one(&entries[i]);
        if (msg == k_oom) {
            return -1;
        }
        if (msg != NULL) {
            entries[i].bad = true;
            entries[i].bad_message = msg;
        }
    }
    // NOLINTNEXTLINE(bugprone-sizeof-expression) an array of pointers: each element is one
    with_id = (ImportMemory **)calloc(n > 0U ? n : 1U, sizeof(*with_id));
    if (with_id == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (!entries[i].bad && entries[i].id != NULL) {
            with_id[nid++] = &entries[i];
        }
    }
    /* O(n log n): a file of a million entries must not take hours (review A). */
    // NOLINTNEXTLINE(bugprone-sizeof-expression) an array of pointers: each element is one
    qsort((void *)with_id, nid, sizeof(*with_id), cmp_id_then_position);
    for (i = 1; i < nid; i++) {
        if (strcmp(with_id[i - 1U]->id, with_id[i]->id) == 0) {
            with_id[i]->bad = true;
            with_id[i]->bad_message = "duplicate id";
        }
    }
    free((void *)with_id);
    return 0;
}

static const char *validate_link(ImportLink *l)
{

    if (l == NULL) {
        return "invalid link";
    }
    if (l->from == NULL || l->to == NULL || l->kind == NULL) {
        return "invalid link";
    }
    if (!uuid_lowercase_in_place(l->from) || !uuid_lowercase_in_place(l->to)) {
        return "invalid link";
    }
    if (strcmp(l->kind, "related") != 0 && strcmp(l->kind, "cites") != 0 &&
        strcmp(l->kind, "supersedes") != 0) {
        return "invalid link";
    }
    if (l->created_at != NULL && !utc_stamp_is_canonical(l->created_at)) {
        return "invalid timestamp";
    }
    if (l->updated_at != NULL && !utc_stamp_is_canonical(l->updated_at)) {
        return "invalid timestamp";
    }
    if (l->deleted_at != NULL && !utc_stamp_is_canonical(l->deleted_at)) {
        return "invalid timestamp";
    }
    return NULL;
}

void import_validate_links(ImportLink *links, size_t n)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        const char *msg = NULL;

        if (links[i].bad) {
            continue;
        }
        msg = validate_link(&links[i]);
        if (msg != NULL) {
            links[i].bad = true;
            links[i].bad_message = msg;
        }
    }
}
