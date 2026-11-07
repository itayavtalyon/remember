#ifndef REMEMBER_OUTPUT_H
#define REMEMBER_OUTPUT_H

#include "store.h"

#include <stdbool.h>
#include <stdio.h>

/* Write one JSON string (including surrounding quotes) to out. Escapes per
 * RFC 8259: ", \, and U+0000–U+001F. Returns 0 on success, -1 on I/O error.
 */
int output_json_string(FILE *out, const char *s);

/* Print a single Entry as a JSON object (no trailing newline). now is required
 * to compute bin. */
int output_entry_json(FILE *out, const Entry *e, const char *now);

/*
 * The *_data writers write the data object of a --json success, with no
 * newline. remember_run wraps it in the one remember-message 1 envelope
 * (008 JE-2.1) with the reply type, any warnings, and the newline.
 */

/* One-memory write (update, rekey): data{action, memory{}}, memory without links. */
int output_action_data(FILE *out, const char *action, const Entry *e, const char *now);

/* data{[action,] memory{... links}}. get passes a NULL action; add passes its action. */
int output_memory_data(FILE *out, const char *action, const Entry *e, const StoreNeighbor *links,
                       size_t nlinks, const char *now);

/* Many-memory write (delete, purge, conflict accept): data{action, count, entries[]}. */
int output_entries_data(FILE *out, const char *action, const Entry *entries, size_t count,
                        const char *now);

/* List data: count, total, entries. Paging lives on the envelope, not here. */
int output_list_data(FILE *out, size_t count, size_t total, const Entry *entries,
                     const StoreNeighbor *links, size_t nlinks, const char *now);

/*
 * search --json: the list data with the keys and tags facets between total and
 * entries (006 E2.6). count, total and entries come from res->page.
 */
int output_search_data(FILE *out, const SearchResult *res, const StoreNeighbor *links,
                       size_t nlinks, const char *now);

/*
 * search human facets (006 E2.7, decision:plan18-human-facet-line): a
 * "keys: " line, then a "tags: " line with "tag (n)" items, each only when it
 * has items. At most 10 items each, masked like every stored string; when the
 * facet total is larger, the line ends ", ... and N more" (U+2026) with
 * N = total - printed. Writes nothing when both facets are empty.
 */
int output_search_facets_human(FILE *out, const SearchResult *res);

/* Human add/get id line. */
int output_id_human(FILE *out, long long id);

/*
 * Write text with each byte that could drive the terminal (C0 except tab,
 * plus DEL) replaced by '?'. keep_newline keeps newlines for bodies; a
 * one-line notice passes false. NULL text writes nothing. No trailing newline.
 * Returns 0 on success, -1 on I/O error.
 */
int output_text_human(FILE *out, const char *text, bool keep_newline);

/*
 * Write a full body for human `get`, then a newline: output_text_human with
 * newlines kept, so multi-line memories render intact. For exact bytes use the
 * --json path. Returns 0 on success, -1 on I/O error.
 */
int output_body_human(FILE *out, const char *body);

/* Human list line: id | key | tags | preview | updated_at | related (ids). */
int output_entry_human_line(FILE *out, const Entry *e, const StoreNeighbor *links, size_t nlinks,
                            const char *now);

/* Tags JSON envelope: version/count/tags:[{name,count}]. */
int output_tags_data(FILE *out, const TagCount *tags, size_t count);

/* Human tags: one "name<TAB>count" line per tag (names are control-free tokens). */
int output_tags_human(FILE *out, const TagCount *tags, size_t count);

/* link/unlink write envelope: action + count + links stubs. now is command utc. */
int output_links_write_data(FILE *out, const char *action, const StoreNeighbor *links, size_t count,
                            const char *now);

/* remember related --json. id is the memory UUID; local_id is the integer. */
int output_related_data(FILE *out, const Entry *subject, const StoreNeighbor *links, size_t count,
                        const char *now);

/* Human Related: block (omit entirely when count==0). */
int output_related_human(FILE *out, const StoreNeighbor *links, size_t count, const char *now);

/* import --json counts envelope (no entry dump). */
int output_import_data(FILE *out, const StoreImportCounts *counts);

/* device: plain id, or data{device_id}. */
int output_device_human(FILE *out, const char *device_id);
int output_device_data(FILE *out, const char *device_id);

/* device new JSON. previous NULL is JSON null (the file was missing). */
int output_device_new_data(FILE *out, const char *device_id, const char *previous);

/* brain new-id data{brain_id, previous_brain_id}. */
int output_brain_new_id_data(FILE *out, const StoreBrainId *ids);

/* conflicts --json list. */
int output_conflicts_data(FILE *out, const StoreConflict *rows, size_t count);

/* repair search-index. JSON is the E4.14 object; human is D8 "rebuilt N". */
int output_rebuilt(FILE *out, bool json, size_t entries);

#endif /* REMEMBER_OUTPUT_H */
