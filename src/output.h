#ifndef REMEMBER_OUTPUT_H
#define REMEMBER_OUTPUT_H

#include "store.h"

#include <stdio.h>

/* Write one JSON string (including surrounding quotes) to out. Escapes per
 * RFC 8259: ", \, and U+0000–U+001F. Returns 0 on success, -1 on I/O error.
 */
int output_json_string(FILE *out, const char *s);

/* Print a single Entry as a JSON object (no trailing newline). */
int output_entry_json(FILE *out, const Entry *e);

/* Uniform mutation envelope: version/action/count/entries:[one]. */
int output_action_envelope(FILE *out, const char *action, const Entry *e);

/* purge-trash JSON: action deleted, count N, all snapshots (no cap). */
int output_deleted_list(FILE *out, const Entry *entries, size_t count);

/* Get envelope: version/count/entries:[one] (no action). links after expires_at. */
int output_get_envelope(FILE *out, const Entry *e, const StoreNeighbor *links, size_t nlinks,
                        const char *now);

/* List/search-style paged envelope. Each entry gets subject-relative stubs. */
int output_list_envelope(FILE *out, size_t offset, size_t limit, size_t count, size_t total,
                         const Entry *entries, const StoreNeighbor *links, size_t nlinks,
                         const char *now);

/* Human add/get id line. */
int output_id_human(FILE *out, long long id);

/*
 * Write a full body for human `get`, then a newline. Control bytes that could
 * drive the terminal (C0 except \n and \t, plus DEL) are replaced with '?';
 * newlines and tabs are kept so multi-line memories render intact. For exact
 * bytes use the --json path. Returns 0 on success, -1 on I/O error.
 */
int output_body_human(FILE *out, const char *body);

/* Human list line: id | key | tags | preview | updated_at | related (ids). */
int output_entry_human_line(FILE *out, const Entry *e, const StoreNeighbor *links, size_t nlinks,
                            const char *now);

/* Tags JSON envelope: version/count/tags:[{name,count}]. */
int output_tags_envelope(FILE *out, const TagCount *tags, size_t count);

/* Human tags: one "name<TAB>count" line per tag (names are control-free tokens). */
int output_tags_human(FILE *out, const TagCount *tags, size_t count);

/* link/unlink write envelope: action + count + links stubs. now is command utc. */
int output_links_write_envelope(FILE *out, const char *action, const StoreNeighbor *links,
                                size_t count, const char *now);

/* remember related --json */
int output_related_envelope(FILE *out, long long id, const char *key, const StoreNeighbor *links,
                            size_t count, const char *now);

/* Human Related: block (omit entirely when count==0). */
int output_related_human(FILE *out, const StoreNeighbor *links, size_t count, const char *now);

#endif /* REMEMBER_OUTPUT_H */
