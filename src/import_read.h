#ifndef REMEMBER_IMPORT_READ_H
#define REMEMBER_IMPORT_READ_H

#include <stdbool.h>
#include <stddef.h>

/*
 * In-memory import document (plan 23). Readers fill this; validate marks bad
 * memories; the store merge writes the good ones. Links and settings land in
 * later stages.
 */

typedef struct {
    char *id;             /* lowercase UUID, or NULL when absent */
    char *key;            /* NULL when absent */
    char *body;           /* required after validate; may be NULL before */
    char **tags;
    size_t ntags;
    char *created_by;     /* canonical actor JSON, or NULL */
    char *created_at;     /* or NULL → now on insert */
    char *updated_at;     /* or NULL → now on insert */
    char *expires_at;     /* or NULL */
    char *deleted_at;     /* or NULL */
    char *version_vector; /* or NULL → "{}" on insert */
    char *file;           /* basename (JSON/CSV) or relative path (MD) */
    const char *bad_message; /* static message when bad */
    int line;             /* 1-based line of the entry's '{' */
    bool bad;
    char pad_[3];         /* explicit tail padding (kept -Wpadded-clean) */
} ImportMemory;

typedef struct {
    char *brain_id; /* lowercase, or NULL when absent */
    ImportMemory *entries;
    size_t nentries;
    /* Whole-file failure (import_parse). parse_message is heap when set. */
    char *parse_message;
    char *parse_fix; /* optional heap fix; NULL → catalog */
    long long version; /* document version when known */
    bool version_too_new;
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} ImportDoc;

void import_doc_free(ImportDoc *doc);

/*
 * Read a remember-export JSON file through fileio + jsonread. On a whole-file
 * failure, returns 0 with doc->parse_message set. On I/O failure returns -1
 * and sets *sys_errno. Settings and links are ignored in stage 2.
 */
int import_read_json(const char *path, ImportDoc *doc, int *sys_errno);

#endif /* REMEMBER_IMPORT_READ_H */
