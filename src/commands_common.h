#ifndef REMEMBER_COMMANDS_COMMON_H
#define REMEMBER_COMMANDS_COMMON_H

/*
 * Shared helpers for command TUs only (cmd_*.c). Not a public API —
 * callers outside the commands layer use commands.h.
 */

#include "normalize.h"
#include "store.h"

#include <stddef.h>

void err_msg(const char *msg);

const char *norm_body_message(NormStatus st);
const char *norm_token_message(NormStatus st, const char *kind);

int source_is_valid(const char *s);
const char *action_name(StoreAddAction a);

/* Growable argv-alias list (tags). Does not own the strings. */
int push_cstr_ptr(const char ***arr, size_t *n, size_t *cap, const char *t);

/* Take the next token as an option value; advances *i. */
int take_value(int *i, int rest_argc, const char **rest_argv, const char **out, const char **err,
               const char *missing_msg);

int normalize_tags(const char *const *tag_raw, size_t ntag_raw, char ***out_tags, size_t *out_ntags,
                   const char **err);
void free_tag_list(char **tags, size_t ntags);

/* Map store status to process exit; prints store_status_message on error. */
int store_status_to_exit(StoreStatus st);

/*
 * Load body from argv token or stdin ("-"). Applies body_trim_copy (64 KiB /
 * UTF-8 / empty). On success *out_body is heap-owned. Returns 0 or -1 with *err.
 */
int load_body(const char *body_raw, char **out_body, size_t *out_len, const char **err);

#endif /* REMEMBER_COMMANDS_COMMON_H */
