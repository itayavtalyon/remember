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

/* Get envelope: version/count/entries:[one] (no action). */
int output_get_envelope(FILE *out, const Entry *e);

/* List/search-style paged envelope. */
int output_list_envelope(FILE *out, size_t offset, size_t limit, size_t count, size_t total,
                         const Entry *entries);

/* Human add/get id line. */
int output_id_human(FILE *out, long long id);

/*
 * Write a full body for human `get`, then a newline. Control bytes that could
 * drive the terminal (C0 except \n and \t, plus DEL) are replaced with '?';
 * newlines and tabs are kept so multi-line memories render intact. For exact
 * bytes use the --json path. Returns 0 on success, -1 on I/O error.
 */
int output_body_human(FILE *out, const char *body);

/* Human list line: id | key | tags | preview | updated_at */
int output_entry_human_line(FILE *out, const Entry *e);

/* Tags JSON envelope: version/count/tags:[{name,count}]. */
int output_tags_envelope(FILE *out, const TagCount *tags, size_t count);

/* Human tags: one "name<TAB>count" line per tag (names are control-free tokens). */
int output_tags_human(FILE *out, const TagCount *tags, size_t count);

#endif /* REMEMBER_OUTPUT_H */
