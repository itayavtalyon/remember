#ifndef REMEMBER_IMPORT_VALIDATE_H
#define REMEMBER_IMPORT_VALIDATE_H

#include "import_read.h"

#include <stddef.h>

/*
 * Pure memory-record checks (plan 23). Marks m->bad and sets m->bad_message.
 * Lowercases a valid id in place. Duplicate ids in the file: the second and
 * later copies are bad ("duplicate id").
 */
int import_validate_memories(ImportMemory *entries, size_t n); /* -1 out of memory */

/* Pure link-record checks. Marks bad with "invalid link" or "invalid timestamp". */
void import_validate_links(ImportLink *links, size_t n);

/* True when s is 8-4-4-4-12 hex (either case). Writes lowercase into out. */
bool import_uuid_normalize(const char *s, char out[37]);

#endif /* REMEMBER_IMPORT_VALIDATE_H */
