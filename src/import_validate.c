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

enum { UUID_TEXT_LEN = 36 };

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

bool import_uuid_normalize(const char *s, char out[37])
{
    static const int dashes[] = {8, 13, 18, 23};
    size_t i = 0;
    size_t d = 0;

    if (s == NULL || out == NULL) {
        return false;
    }
    for (i = 0; i < (size_t)UUID_TEXT_LEN; i++) {
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
        out[i] = (char)tolower(c);
    }
    if (s[UUID_TEXT_LEN] != '\0') {
        return false;
    }
    out[UUID_TEXT_LEN] = '\0';
    return true;
}

static bool token_ok(const char *token)
{
    char buf[REMEMBER_TOKEN_MAX + 1];

    if (token == NULL) {
        return false;
    }
    if (normalize_token(token, buf, sizeof(buf)) != NORM_OK) {
        return false;
    }
    return true;
}

static const char *validate_one(ImportMemory *m)
{
    char id_buf[37];
    size_t i = 0;
    NormStatus bst = NORM_OK;
    char *trimmed = NULL;
    size_t tlen = 0;

    if (m == NULL) {
        return "entry is not an object";
    }
    if (m->id != NULL) {
        if (!import_uuid_normalize(m->id, id_buf)) {
            return "id is not a UUID";
        }
        if (strcmp(m->id, id_buf) != 0) {
            char *copy = (char *)malloc(sizeof(id_buf));

            if (copy == NULL) {
                return "id is not a UUID"; /* OOM surfaced as bad; store sees OOM elsewhere */
            }
            memcpy(copy, id_buf, sizeof(id_buf));
            free(m->id);
            m->id = copy;
        }
    }
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
    if (m->key != NULL && !token_ok(m->key)) {
        return "invalid key or tag";
    }
    for (i = 0; i < m->ntags; i++) {
        if (!token_ok(m->tags[i])) {
            return "invalid key or tag";
        }
    }
    if (m->ntags > 1U) {
        qsort(m->tags, m->ntags, sizeof(*m->tags), cmp_tag);
    }
    if (m->created_at != NULL && !utc_stamp_is_canonical(m->created_at)) {
        return "invalid timestamp";
    }
    if (m->updated_at != NULL && !utc_stamp_is_canonical(m->updated_at)) {
        return "invalid timestamp";
    }
    if (m->expires_at != NULL && !utc_stamp_is_canonical(m->expires_at)) {
        return "invalid timestamp";
    }
    if (m->deleted_at != NULL && !utc_stamp_is_canonical(m->deleted_at)) {
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

void import_validate_memories(ImportMemory *entries, size_t n)
{
    size_t i = 0;
    size_t j = 0;

    for (i = 0; i < n; i++) {
        const char *msg = NULL;

        if (entries[i].bad) {
            continue;
        }
        msg = validate_one(&entries[i]);
        if (msg != NULL) {
            entries[i].bad = true;
            entries[i].bad_message = msg;
            continue;
        }
        if (entries[i].id == NULL) {
            continue;
        }
        for (j = 0; j < i; j++) {
            if (!entries[j].bad && entries[j].id != NULL &&
                strcmp(entries[j].id, entries[i].id) == 0) {
                entries[i].bad = true;
                entries[i].bad_message = "duplicate id";
                break;
            }
        }
    }
}
