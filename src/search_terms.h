#ifndef REMEMBER_SEARCH_TERMS_H
#define REMEMBER_SEARCH_TERMS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Search terms (006 E2.2 steps 1-3, E2.5, E2.6; D12, D28, P2). Pure: no I/O,
 * no SQLite. The store adapter quotes the tokens into P and runs the SQL.
 */

enum {
    SEARCH_TOKENS_MAX = 16,       /* E2.2 step 2 */
    SEARCH_TOKEN_CPS_MAX = 64,    /* E2.2 step 2 */
    SEARCH_TOKEN_BYTES_MAX = 256, /* 64 code points x 4 UTF-8 bytes */
    SEARCH_LABEL_GRAPHEMES = 60   /* E2.6 label */
};

typedef enum {
    SEARCH_OK = 0,
    SEARCH_ERR_INVALID_UTF8,
    SEARCH_ERR_OOM,
    SEARCH_ERR_INTERNAL /* caller contract violation */
} SearchStatus;

/* E2.6 key tiers; also the D28 tag tiers. NONE means no text-match (E2.5b). */
typedef enum { SEARCH_TIER_NONE = 0, SEARCH_TIER_1, SEARCH_TIER_2, SEARCH_TIER_3 } SearchTier;

/* The P tokens t1..tn, ASCII-lowercased (E2.5), in query order. */
typedef struct {
    size_t count;
    char tokens[SEARCH_TOKENS_MAX][SEARCH_TOKEN_BYTES_MAX + 1];
} SearchTokens;

/*
 * E2.2 steps 1-3 on query (NUL-terminated): NFC, split on Unicode White_Space
 * and every ASCII byte outside [A-Za-z0-9], drop empty tokens, keep the first
 * 16 tokens and the first 64 code points of each, remove the E10.1 Hebrew
 * marks, then drop a token that the fold emptied (decision:plan18-empty-token-
 * no-vacuous). Invalid UTF-8 is SEARCH_ERR_INVALID_UTF8 (D27) and leaves
 * *out untouched.
 */
SearchStatus search_tokens_build(const char *query, SearchTokens *out);

/*
 * q (E2.4): query trimmed of ASCII whitespace and ASCII-lowercased, into out.
 * out_cap must be at least strlen(query) + 1.
 */
SearchStatus search_q(const char *query, char *out, size_t out_cap);

/* One facet candidate: a stored key or tag, classified against the query. */
typedef struct {
    const char *text; /* the stored bytes; not copied */
    long long count;  /* tags: memories in scope that carry it (E2.6) */
    SearchTier tier;
    bool exact;   /* text equals q (D28: an exact tag sorts first) */
    char pad_[3]; /* explicit tail padding (kept -Wpadded-clean) */
} SearchFacet;

/*
 * E2.5 text-match and E2.6 tier of text. Tier 1: q is a non-empty prefix of
 * text. Otherwise text is NFC'd and Hebrew-folded for matching
 * (decision:plan18-segment-normalize) and split into segments on ':', '-',
 * '_', '.', '/' and Unicode White_Space
 * (decision:plan18-segment-unicode-whitespace). Tier 2: at least one token,
 * and each token starts a segment at an index >= the previous token's. Tier 3:
 * every token starts some segment. No tokens and no tier 1 is NONE
 * (decision:plan18-empty-token-no-vacuous). Writes *out on SEARCH_OK only.
 */
SearchStatus search_facet_classify(const SearchTokens *tokens, const char *q, const char *text,
                                   long long count, SearchFacet *out);

/* qsort order for keys (E2.6): tier, then text in code point order. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) qsort's signature
int search_key_order(const void *a, const void *b);

/* qsort order for tags (D28): exact, then tier, then count DESC, then text. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) qsort's signature
int search_tag_order(const void *a, const void *b);

/*
 * E2.6 label (D12, P2). The title when body's first line starts with "# "
 * (the rest of that line, ASCII-trimmed, if not empty); else key when not
 * NULL; else the first 60 grapheme clusters of the plain text (body with every
 * run of White_Space or Cc code points collapsed to one space, then trimmed),
 * plus U+2026 only when the cut truncates. *out is heap (free it).
 */
SearchStatus search_label(const char *body, const char *key, char **out);

#endif /* REMEMBER_SEARCH_TERMS_H */
