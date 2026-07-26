#include "commands_common.h"

#include "exit_codes.h"
#include "normalize.h"
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *norm_body_message(NormStatus st)
{
    switch (st) {
    case NORM_OK:
        return "ok";
    case NORM_ERR_EMPTY:
        return "empty body after trim";
    case NORM_ERR_TOO_LONG:
        return "body exceeds 64 KiB";
    case NORM_ERR_INVALID_UTF8:
        return "invalid UTF-8 in body";
    case NORM_ERR_OOM:
        return "out of memory";
    case NORM_ERR_INVALID_CHAR:
    case NORM_ERR_INTERNAL:
    default:
        return "invalid body";
    }
}

const char *norm_token_message(NormStatus st, const char *kind)
{
    int is_key = (kind != NULL && strcmp(kind, "key") == 0);

    switch (st) {
    case NORM_OK:
        return "ok";
    case NORM_ERR_EMPTY:
        return is_key ? "empty key" : "empty tag";
    case NORM_ERR_TOO_LONG:
        return is_key ? "key exceeds 64 bytes" : "tag exceeds 64 bytes";
    case NORM_ERR_INVALID_UTF8:
        return is_key ? "invalid UTF-8 in key" : "invalid UTF-8 in tag";
    case NORM_ERR_INVALID_CHAR:
        return is_key ? "invalid key" : "invalid tag";
    case NORM_ERR_OOM:
        return "out of memory";
    case NORM_ERR_INTERNAL:
    default:
        return is_key ? "invalid key" : "invalid tag";
    }
}

void err_msg(const char *msg)
{
    (void)fprintf(stderr, "remember: %s\n", msg);
}

int source_is_valid(const char *s)
{
    return s != NULL && (strcmp(s, "human") == 0 || strcmp(s, "agent") == 0 ||
                         strcmp(s, "tool") == 0 || strcmp(s, "unknown") == 0);
}

const char *action_name(StoreAddAction a)
{
    switch (a) {
    case STORE_ADD_CREATED:
        return "created";
    case STORE_ADD_MERGED:
        return "merged";
    case STORE_ADD_UPDATED:
        return "updated";
    default:
        return "created";
    }
}

int push_cstr_ptr(const char ***arr, size_t *n, size_t *cap, const char *t)
{
    if (*n == *cap) {
        size_t ncap = (*cap == 0U) ? 4U : (*cap * 2U);
        const char **grown = (const char **)realloc((void *)*arr, ncap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        *arr = grown;
        *cap = ncap;
    }
    (*arr)[*n] = t;
    (*n)++;
    return 0;
}

int take_value(int *i, int rest_argc, const char **rest_argv, const char **out, const char **err,
               const char *missing_msg)
{
    if (*i + 1 >= rest_argc) {
        *err = missing_msg;
        return -1;
    }
    *i += 1;
    *out = rest_argv[*i];
    return 0;
}

/* ISO C11: avoid strdup (POSIX; hidden under -std=c11 without feature macros). */
static char *dup_cstr(const char *s)
{
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    p = malloc(n + 1U);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n + 1U);
    return p;
}

int normalize_tags(const char *const *tag_raw, size_t ntag_raw, char ***out_tags, size_t *out_ntags,
                   const char **err)
{
    size_t t;
    char **tags;

    *out_tags = NULL;
    *out_ntags = 0U;
    *err = NULL;
    if (ntag_raw == 0U) {
        return 0;
    }

    tags = (char **)calloc(ntag_raw, sizeof(*tags));
    if (tags == NULL) {
        *err = "out of memory";
        return -1;
    }
    for (t = 0; t < ntag_raw; t++) {
        char buf[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_tag(tag_raw[t], buf, sizeof(buf));
        if (ns != NORM_OK) {
            size_t j;
            for (j = 0; j < t; j++) {
                free(tags[j]);
            }
            free((void *)tags);
            *err = norm_token_message(ns, "tag");
            return -1;
        }
        tags[t] = dup_cstr(buf);
        if (tags[t] == NULL) {
            size_t j;
            for (j = 0; j < t; j++) {
                free(tags[j]);
            }
            free((void *)tags);
            *err = "out of memory";
            return -1;
        }
    }
    *out_tags = tags;
    *out_ntags = ntag_raw;
    return 0;
}

void free_tag_list(char **tags, size_t ntags)
{
    size_t t;
    if (tags == NULL) {
        return;
    }
    for (t = 0; t < ntags; t++) {
        free(tags[t]);
    }
    free((void *)tags);
}

int store_status_to_exit(StoreStatus st)
{
    if (st == STORE_ERR_NOT_FOUND) {
        err_msg(store_status_message(st));
        return REMEMBER_NOT_FOUND;
    }
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}
