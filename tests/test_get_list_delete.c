#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SEED_ROWS = 25,     /* rows seeded for the list/paging tests */
    BODY_BUFSIZE = 64,  /* scratch body buffer */
    TAG_PAIRS_OVER = 51 /* one past the 50-tag-filter limit */
};

static char *take_next_cursor(const char *json);

static void seed_three(const char *db)
{
    CmdResult r;
    const char *a1[] = {"add", "--tag", "a", "--as", "human", "alpha first"};
    const char *a2[] = {"add", "--tag", "b", "--as", "agent", "beta second"};
    const char *a3[] = {"add", "--tag", "a", "--tag", "b", "--as", "tool", "gamma third"};
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    cmd_result_free(&r);
    /* ensure updated_at ordering: tiny sleep not portable enough; rely on
     * sequential updates via update --text later when implemented. Order of
     * add is still updated_at increasing. */
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a3, sizeof(a3) / sizeof(a3[0]), NULL);
    cmd_result_free(&r);
}

TEST(get_existing_json_envelope)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "--tag", "t", "get me"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_PREFIX(g.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get."
                             "result\",\"data\":{\"memory\":{");
    ASSERT_STR_CONTAINS(g.out, "get me");
    ASSERT_STR_CONTAINS(g.out, "\"local_id\":1");
    cmd_result_free(&g);
    free(db);
}

TEST(get_missing_exits_two)
{
    char *db = make_temp_db_path();
    const char *gargs[] = {"get", "99"};
    CmdResult g;
    ASSERT_TRUE(db != NULL);
    /* empty db still valid path */
    {
        const char *a[] = {"add", "placeholder"};
        CmdResult r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
        cmd_result_free(&r);
    }
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    cmd_result_free(&g);
    free(db);
}

TEST(get_human_shows_body)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "human get body"};
    const char *gargs[] = {"get", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "human get body");
    cmd_result_free(&g);
    free(db);
}

TEST(list_empty_exits_zero)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    /* create empty db by listing before any add — or add+delete.
     * list on missing file should create schema and return empty. */
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(list_default_order_updated_at_desc)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    /* newest first: id 3 before id 1 in JSON text order of entries array */
    {
        const char *p3 = strstr(r.out, "\"local_id\":3");
        const char *p1 = strstr(r.out, "\"local_id\":1");
        ASSERT_TRUE(p3 != NULL && p1 != NULL);
        ASSERT_TRUE(p3 < p1);
    }
    cmd_result_free(&r);
    free(db);
}

TEST(list_filter_tag_and)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--tag", "a", "--tag", "b"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "gamma third");
    ASSERT_STR_NOT_CONTAINS(r.out, "alpha first");
    ASSERT_STR_NOT_CONTAINS(r.out, "beta second");
    cmd_result_free(&r);
    free(db);
}

TEST(list_filter_source)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--created-by", "agent"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "beta second");
    ASSERT_STR_NOT_CONTAINS(r.out, "alpha first");
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--limit", "2"};
    int count = 0;
    const char *p = NULL;
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    p = r.out;
    while ((p = strstr(p, "\"local_id\":")) != NULL) {
        count++;
        p += sizeof("\"local_id\":") - 1;
    }
    ASSERT_EQ_INT(count, 2);
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_default_is_twenty)
{
    char *db = make_temp_db_path();
    CmdResult r;
    int i = 0;
    int count = 0;
    const char *p = NULL;
    const char *largs[] = {"list", "--json"};
    ASSERT_TRUE(db != NULL);
    for (i = 0; i < SEED_ROWS; i++) {
        char body[BODY_BUFSIZE];
        const char *a[2];
        CmdResult ar;
        (void)snprintf(body, sizeof(body), "item number %d unique", i);
        a[0] = "add";
        a[1] = body;
        ar = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
        cmd_result_free(&ar);
    }
    r = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    p = r.out;
    while ((p = strstr(p, "\"local_id\":")) != NULL) {
        count++;
        p += sizeof("\"local_id\":") - 1;
    }
    ASSERT_EQ_INT(count, 20);
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_zero_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--limit", "0"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_without_a_value_is_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--limit"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: missing value for --limit\n");
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_accepts_the_hard_cap)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--limit", "1000"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(db);
}

/* --facet-limit is a search option; list must not take it silently. */
TEST(list_rejects_facet_limit)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--facet-limit", "5"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "remember: unknown option '--facet-limit'\n");
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_over_hard_cap_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--limit", "1001"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

/*
 * A tag containing a SQL metacharacter (single quote) must be a bind parameter,
 * never spliced into SQL: it stores, the list filter matches it exactly, and the
 * schema is untouched. Regression guard for the parameterized WHERE builder.
 */
TEST(list_filter_tag_with_quote_is_parameterized)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "--tag", "a'b", "quoted tag"};
    const char *l[] = {"list", "--json", "--tag", "a'b"};
    const char *l2[] = {"list", "--json", "--tag", "zzz"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, l, sizeof(l) / sizeof(l[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"total\":1");
    ASSERT_STR_CONTAINS(r.out, "\"quoted tag\"");
    cmd_result_free(&r);

    /* A non-matching filter returns nothing — proof the quote was not injected. */
    r = run_remember(db, l2, sizeof(l2) / sizeof(l2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"total\":0");
    cmd_result_free(&r);
    free(db);
}

/*
 * A cursor past the last remaining row returns an empty page but the unpaged
 * total. This is the case a single COUNT(*) OVER() query would get wrong
 * (empty page → no total), so it pins the two statements in one transaction.
 */
TEST(list_cursor_past_end_keeps_total)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char *cursor = NULL;
    const char *page[] = {"list", "--json", "--limit", "1"};
    const char *drop1[] = {"delete", "1"};
    const char *drop2[] = {"delete", "2"};

    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, page, sizeof(page) / sizeof(page[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cursor = take_next_cursor(r.out);
    ASSERT_TRUE(cursor != NULL);
    cmd_result_free(&r);
    r = run_remember(db, drop1, sizeof(drop1) / sizeof(drop1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, drop2, sizeof(drop2) / sizeof(drop2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        const char *next[] = {"list", "--json", "--limit", "1", "--cursor", cursor};

        r = run_remember(db, next, sizeof(next) / sizeof(next[0]), NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"count\":0");
    ASSERT_STR_CONTAINS(r.out, "\"total\":1");
    ASSERT_STR_CONTAINS(r.out, "\"entries\":[]");
    ASSERT_STR_CONTAINS(r.out, "\"next_cursor\":null");
    ASSERT_STR_CONTAINS(r.out, "\"has_more\":false");
    cmd_result_free(&r);
    free(cursor);
    free(db);
}

/* Over-cap tag filters are a clear user error, not an opaque "internal error". */
TEST(list_too_many_tag_filters_rejected)
{
    char *db = make_temp_db_path();
    const char *args[1 + (TAG_PAIRS_OVER * 2)];
    CmdResult r;
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    args[0] = "list";
    for (i = 0; i < TAG_PAIRS_OVER; i++) {
        args[1U + (i * 2U)] = "--tag";
        args[2U + (i * 2U)] = "t";
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "too many --tag");
    ASSERT_STR_NOT_CONTAINS(r.err, "internal");
    cmd_result_free(&r);
    free(db);
}

TEST(delete_existing)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    CmdResult g;
    const char *a[] = {"add", "to delete"};
    const char *dargs[] = {"delete", "1"};
    const char *gargs[] = {"get", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, sizeof(dargs) / sizeof(dargs[0]), NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    cmd_result_free(&d);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 3);
    ASSERT_STREQ(g.err, "remember: deleted\n");
    cmd_result_free(&g);
    {
        const char *get_deleted[] = {"get", "--deleted", "1"};
        g = run_remember(db, get_deleted, sizeof(get_deleted) / sizeof(get_deleted[0]), NULL);
        ASSERT_EQ_INT(g.exit_code, 0);
        ASSERT_STR_CONTAINS(g.out, "to delete");
        cmd_result_free(&g);
    }
    free(db);
}

TEST(delete_missing_exits_two)
{
    char *db = make_temp_db_path();
    const char *dargs[] = {"delete", "42"};
    CmdResult d;
    ASSERT_TRUE(db != NULL);
    {
        const char *a[] = {"add", "x"};
        CmdResult r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
        cmd_result_free(&r);
    }
    d = run_remember(db, dargs, sizeof(dargs) / sizeof(dargs[0]), NULL);
    ASSERT_EQ_INT(d.exit_code, 2);
    cmd_result_free(&d);
    free(db);
}

TEST(delete_json_shape)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    const char *a[] = {"add", "bye"};
    const char *dargs[] = {"delete", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, sizeof(dargs) / sizeof(dargs[0]), NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    ASSERT_STR_CONTAINS(d.out, "\"version\":1");
    ASSERT_STR_CONTAINS(d.out, "\"action\":\"deleted\"");
    ASSERT_STR_CONTAINS(d.out, "\"count\":1");
    ASSERT_STR_CONTAINS(d.out, "\"entries\"");
    ASSERT_STR_CONTAINS(d.out, "bye");
    cmd_result_free(&d);
    free(db);
}

/* Copy the opaque next_cursor string. NULL when the field is absent or null. */
static char *take_next_cursor(const char *json)
{
    const char *key = "\"next_cursor\":\"";
    const char *start = NULL;
    const char *end = NULL;
    char *out = NULL;
    size_t n = 0U;

    if (json == NULL) {
        return NULL;
    }
    start = strstr(json, key);
    if (start == NULL) {
        return NULL;
    }
    start += strlen(key);
    end = strchr(start, '"');
    if (end == NULL) {
        return NULL;
    }
    n = (size_t)(end - start);
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

/* Three live rows, one per page. The cursor is opaque; the rows stay in list order. */
TEST(list_json_cursor_pages)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char *cursor = NULL;
    const char *page1[] = {"list", "--json", "--limit", "1"};
    const char *garbage[] = {"list", "--json", "--cursor", "!!!!"};
    const char *search[] = {"search", "--json", "alpha"};
    const char *offset[] = {"list", "--offset"};

    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, page1, sizeof(page1) / sizeof(page1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "\"total\":3");
    ASSERT_STR_CONTAINS(r.out, "\"has_more\":true");
    ASSERT_STR_CONTAINS(r.out, "\"local_id\":3");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"local_id\":2");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"offset\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"limit\"");
    cursor = take_next_cursor(r.out);
    ASSERT_TRUE(cursor != NULL && cursor[0] != '\0');
    cmd_result_free(&r);

    {
        const char *page2[] = {"list", "--json", "--limit", "1", "--cursor", cursor};

        r = run_remember(db, page2, sizeof(page2) / sizeof(page2[0]), NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "\"total\":3");
    ASSERT_STR_CONTAINS(r.out, "\"has_more\":true");
    ASSERT_STR_CONTAINS(r.out, "\"local_id\":2");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"local_id\":3");
    free(cursor);
    cursor = take_next_cursor(r.out);
    ASSERT_TRUE(cursor != NULL && cursor[0] != '\0');
    cmd_result_free(&r);

    {
        const char *page3[] = {"list", "--json", "--limit", "1", "--cursor", cursor};

        r = run_remember(db, page3, sizeof(page3) / sizeof(page3[0]), NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "\"total\":3");
    ASSERT_STR_CONTAINS(r.out, "\"local_id\":1");
    ASSERT_STR_CONTAINS(r.out, "\"next_cursor\":null");
    ASSERT_STR_CONTAINS(r.out, "\"has_more\":false");
    cmd_result_free(&r);

    {
        const char *bad_sort[] = {"list",   "--json",  "--limit",  "1",
                                  "--sort", "created", "--cursor", cursor};

        r = run_remember(db, bad_sort, sizeof(bad_sort) / sizeof(bad_sort[0]), NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"invalid_argument\"");
    ASSERT_STR_CONTAINS(r.out, "invalid --cursor");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"has_more\"");
    cmd_result_free(&r);
    free(cursor);

    r = run_remember(db, garbage, sizeof(garbage) / sizeof(garbage[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"invalid_argument\"");
    ASSERT_STR_CONTAINS(r.out, "invalid --cursor");
    cmd_result_free(&r);

    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_NOT_CONTAINS(r.out, "next_cursor");
    ASSERT_STR_NOT_CONTAINS(r.out, "has_more");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"offset\"");
    cmd_result_free(&r);

    r = run_remember(db, offset, sizeof(offset) / sizeof(offset[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option");
    cmd_result_free(&r);
    free(db);
}

/* Same layout as the production encoder. The shifts are the encoding. */
// NOLINTBEGIN(hicpp-signed-bitwise,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
static char *b64_encode(const char *text)
{
    static const char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = strlen(text);
    size_t out_len = 4U * ((len + 2U) / 3U);
    char *out = (char *)malloc(out_len + 1U);
    size_t i = 0U;
    size_t j = 0U;

    if (out == NULL) {
        return NULL;
    }
    while (i + 3U <= len) {
        unsigned n = ((unsigned)(unsigned char)text[i] << 16) |
                     ((unsigned)(unsigned char)text[i + 1U] << 8) |
                     (unsigned)(unsigned char)text[i + 2U];
        out[j++] = alpha[(n >> 18) & 63U];
        out[j++] = alpha[(n >> 12) & 63U];
        out[j++] = alpha[(n >> 6) & 63U];
        out[j++] = alpha[n & 63U];
        i += 3U;
    }
    if (i < len) {
        unsigned n = (unsigned)(unsigned char)text[i] << 16;
        if (i + 1U < len) {
            n |= (unsigned)(unsigned char)text[i + 1U] << 8;
        }
        out[j++] = alpha[(n >> 18) & 63U];
        out[j++] = alpha[(n >> 12) & 63U];
        out[j++] = (i + 1U < len) ? alpha[(n >> 6) & 63U] : '=';
        out[j++] = '=';
    }
    out[j] = '\0';
    return out;
}
// NOLINTEND(hicpp-signed-bitwise,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/* db and cursor are both paths-or-text. Callers pass them in that order. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void assert_bad_cursor(const char *db, const char *cursor)
{
    const char *args[] = {"list", "--json", "--cursor", cursor};
    CmdResult r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"invalid_argument\"");
    ASSERT_STR_CONTAINS(r.out, "invalid --cursor");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"has_more\"");
    cmd_result_free(&r);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void assert_bad_json_cursor(const char *db, const char *json)
{
    char *cursor = b64_encode(json);

    ASSERT_TRUE(cursor != NULL);
    if (cursor == NULL) {
        return;
    }
    assert_bad_cursor(db, cursor);
    free(cursor);
}

/* Filters that change the query travel inside the cursor. Limit does not.
   Argv slot indexes are positions in the command tables in this test. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST(list_json_cursor_filters_and_rejects)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char *cursor = NULL;
    char *tagged = NULL;
    const char *key_cursor = "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"key\":\"pref:one\","
                             "\"at\":\"t\",\"local_id\":1}";
    const char *bad_json[] = {
        "{\"sort\":\"updated\",\"v\":1}",
        "{\"v\"1}",
        "{\"v\":1\"sort\":\"updated\"}",
        "{\"v\":}",
        "{\"v\":01}",
        "{\"v\":2}",
        "{\"v\":9223372036854775808}",
        "{\"v\":1,\"sort\":1}",
        "{\"v\":1,\"sort\":\"",
        "{\"v\":1,\"sort\":\"\"}",
        "{\"v\":1,\"sort\":\"nope\"}",
        "{\"v\":1,\"sort\":\"\\n\\r\\t\\b\\f\\\"\\\\\\/\\u0041\\u00aA\"}",
        "{\"v\":1,\"sort\":\"\\u00GG\"}",
        "{\"v\":1,\"sort\":\"\\u0080\"}",
        "{\"v\":1,\"sort\":\"\\q\"}",
        "{\"v\":1,\"sort\":\"a\nb\"}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"nope\"}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"tags\":[]}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"tags\":\"a\"}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"tags\":[\"b\",\"a\"]}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"tags\":[\"\"]}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"tags\":[\"a\"}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"tags\"1}",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"at\":\"t\",\"local_id\":1,",
        "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"at\":\"t\",\"local_id\":01}",
        key_cursor,
        "[]",
        "   ",
    };
    const char *bad_raw[] = {"!!!", "++++", "////", "====", "YR==", "YWJ=", "QQ==QQ=="};
    size_t i = 0U;
    const char *add1[] = {"add", "one"};
    const char *add2[] = {"add", "two"};
    const char *add3[] = {"add", "three"};
    const char *page1[] = {"list", "--json", "--limit", "1"};
    const char *wider[] = {"list", "--json", "--limit", "2", "--sort", "updated", "--cursor", NULL};
    const char *plain[] = {"list", "--limit", "1", "--cursor", NULL};
    const char *dup[] = {"list", "--cursor", "aaaa", "--cursor", "bbbb"};
    const char *missing[] = {"list", "--cursor"};
    const char *search[] = {"search", "--cursor", "aaaa", "alpha"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add1, sizeof(add1) / sizeof(add1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, add2, sizeof(add2) / sizeof(add2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, add3, sizeof(add3) / sizeof(add3[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, page1, sizeof(page1) / sizeof(page1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cursor = take_next_cursor(r.out);
    ASSERT_TRUE(cursor != NULL);
    cmd_result_free(&r);

    wider[7] = cursor;
    r = run_remember(db, wider, sizeof(wider) / sizeof(wider[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"count\":2");
    ASSERT_STR_CONTAINS(r.out, "\"total\":3");
    ASSERT_STR_CONTAINS(r.out, "\"local_id\":2");
    ASSERT_STR_CONTAINS(r.out, "\"local_id\":1");
    ASSERT_STR_CONTAINS(r.out, "\"has_more\":false");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"local_id\":3");
    cmd_result_free(&r);

    plain[4] = cursor;
    r = run_remember(db, plain, sizeof(plain) / sizeof(plain[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "two");
    ASSERT_STR_NOT_CONTAINS(r.out, "next_cursor");
    ASSERT_STR_NOT_CONTAINS(r.out, "three");
    cmd_result_free(&r);
    free(cursor);
    cursor = NULL;

    {
        const char *t1[] = {"add", "--tag", "x", "--tag", "aa", "--as", "agent", "tag one"};
        const char *t2[] = {"add", "--tag", "x", "--tag", "aa", "--as", "agent", "tag two"};
        const char *p1[] = {
            "list", "--json",       "--limit", "1",       "--tag",      "aa",      "--tag",
            "x",    "--created-by", "agent",   "--since", "2000-01-01", "--until", "2999-01-01"};
        const char *p2[16];
        const char *swap[] = {"list",         "--json",     "--limit",  "1",
                              "--tag",        "x",          "--tag",    "aa",
                              "--created-by", "agent",      "--since",  "2000-01-01",
                              "--until",      "2999-01-01", "--cursor", NULL};
        const char *only_x[] = {"list", "--json", "--limit", "1", "--tag", "x"};
        const char *other_source[] = {"list", "--json", "--created-by", "human", "--cursor", NULL};

        r = run_remember(db, t1, sizeof(t1) / sizeof(t1[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, t2, sizeof(t2) / sizeof(t2[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, p1, sizeof(p1) / sizeof(p1[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"has_more\":true");
        tagged = take_next_cursor(r.out);
        ASSERT_TRUE(tagged != NULL);
        cmd_result_free(&r);

        memcpy((void *)p2, (const void *)p1, sizeof(p1));
        p2[14] = "--cursor";
        p2[15] = tagged;
        r = run_remember(db, p2, 16U, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STREQ(r.err, "");
        ASSERT_STR_CONTAINS(r.out, "\"count\":1");
        ASSERT_STR_CONTAINS(r.out, "tag one");
        ASSERT_STR_CONTAINS(r.out, "\"has_more\":false");
        cmd_result_free(&r);

        swap[15] = tagged;
        r = run_remember(db, swap, sizeof(swap) / sizeof(swap[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "tag one");
        cmd_result_free(&r);

        other_source[5] = tagged;
        r = run_remember(db, other_source, sizeof(other_source) / sizeof(other_source[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.out, "\"code\":\"invalid_argument\"");
        cmd_result_free(&r);
        {
            const char *notags[] = {"list",       "--json",  "--created-by", "agent",    "--since",
                                    "2000-01-01", "--until", "2999-01-01",   "--cursor", NULL};
            const char *wrong[] = {"list",    "--json",       "--tag",    "yy",      "--tag",
                                   "zz",      "--created-by", "agent",    "--since", "2000-01-01",
                                   "--until", "2999-01-01",   "--cursor", NULL};

            notags[9] = tagged;
            wrong[13] = tagged;
            r = run_remember(db, notags, sizeof(notags) / sizeof(notags[0]), NULL);
            ASSERT_EQ_INT(r.exit_code, 1);
            ASSERT_STR_CONTAINS(r.out, "\"code\":\"invalid_argument\"");
            cmd_result_free(&r);
            r = run_remember(db, wrong, sizeof(wrong) / sizeof(wrong[0]), NULL);
            ASSERT_EQ_INT(r.exit_code, 1);
            ASSERT_STR_CONTAINS(r.out, "\"code\":\"invalid_argument\"");
            cmd_result_free(&r);
        }
        free(tagged);
        tagged = NULL;

        r = run_remember(db, only_x, sizeof(only_x) / sizeof(only_x[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        tagged = take_next_cursor(r.out);
        ASSERT_TRUE(tagged != NULL);
        cmd_result_free(&r);
        free(tagged);
        tagged = NULL;
    }

    {
        const char *e1[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old one"};
        const char *e2[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old two"};
        const char *expired_list[] = {"list", "--json", "--expired", "--limit", "1"};
        const char *trash[] = {"list", "--json", "--trash", "--limit", "1", "--cursor", NULL};
        const char *d1[] = {"add", "--key", "gone:one", "gone one"};
        const char *d2[] = {"add", "--key", "gone:two", "gone two"};
        const char *del1[] = {"delete", "--key", "gone:one"};
        const char *del2[] = {"delete", "--key", "gone:two"};
        const char *deleted_list[] = {"list", "--json", "--deleted", "--limit", "1"};
        const char *deleted_page[] = {"list", "--json",   "--deleted", "--limit",
                                      "1",    "--cursor", NULL};
        const char *created[] = {"list", "--json", "--sort", "created", "--limit", "1"};
        const char *created2[] = {"list",    "--json", "--sort",   "created",
                                  "--limit", "1",      "--cursor", NULL};

        r = run_remember(db, e1, sizeof(e1) / sizeof(e1[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, e2, sizeof(e2) / sizeof(e2[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, expired_list, sizeof(expired_list) / sizeof(expired_list[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cursor = take_next_cursor(r.out);
        ASSERT_TRUE(cursor != NULL);
        cmd_result_free(&r);
        trash[6] = cursor;
        r = run_remember(db, trash, sizeof(trash) / sizeof(trash[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "old one");
        cmd_result_free(&r);
        free(cursor);

        r = run_remember(db, d1, sizeof(d1) / sizeof(d1[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, d2, sizeof(d2) / sizeof(d2[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, del1, sizeof(del1) / sizeof(del1[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, del2, sizeof(del2) / sizeof(del2[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, deleted_list, sizeof(deleted_list) / sizeof(deleted_list[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"has_more\":true");
        cursor = take_next_cursor(r.out);
        ASSERT_TRUE(cursor != NULL);
        cmd_result_free(&r);
        deleted_page[6] = cursor;
        r = run_remember(db, deleted_page, sizeof(deleted_page) / sizeof(deleted_page[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "gone one");
        cmd_result_free(&r);
        free(cursor);

        r = run_remember(db, created, sizeof(created) / sizeof(created[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cursor = take_next_cursor(r.out);
        ASSERT_TRUE(cursor != NULL);
        cmd_result_free(&r);
        created2[7] = cursor;
        r = run_remember(db, created2, sizeof(created2) / sizeof(created2[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
        free(cursor);
        cursor = NULL;
    }

    for (i = 0U; i < sizeof(bad_json) / sizeof(bad_json[0]); i++) {
        assert_bad_json_cursor(db, bad_json[i]);
    }
    for (i = 0U; i < sizeof(bad_raw) / sizeof(bad_raw[0]); i++) {
        assert_bad_cursor(db, bad_raw[i]);
    }
    {
        char *json = (char *)malloc(4096U);

        ASSERT_TRUE(json != NULL);
        if (json != NULL) {
            int wrote =
                snprintf(json, 4096U, "{\"v\":1,\"sort\":\"updated\",\"bin\":\"live\",\"tags\":[");
            size_t used = (size_t)wrote;

            ASSERT_TRUE(wrote > 0);
            for (i = 0U; i < 51U; i++) {
                wrote =
                    snprintf(json + used, 4096U - used, "%s\"a%02zu\"", (i == 0U) ? "" : ",", i);
                ASSERT_TRUE(wrote > 0);
                used += (size_t)wrote;
            }
            wrote = snprintf(json + used, 4096U - used, "],\"at\":\"t\",\"local_id\":1}");
            ASSERT_TRUE(wrote > 0);
            assert_bad_json_cursor(db, json);
            free(json);
        }
    }

    r = run_remember(db, dup, sizeof(dup) / sizeof(dup[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "duplicate --cursor");
    cmd_result_free(&r);
    r = run_remember(db, missing, sizeof(missing) / sizeof(missing[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "missing value for --cursor");
    cmd_result_free(&r);
    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option '--cursor'");
    cmd_result_free(&r);
    free(db);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(list_offset_is_an_unknown_option)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--offset", "-1"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option '--offset'");
    cmd_result_free(&r);
    free(db);
}

TEST(json_entry_includes_key_field_null_when_keyless)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "keyless entry"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"key\":null");
    cmd_result_free(&g);
    free(db);
}

TEST(get_expired_exits_three)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "gone"};
    const char *gargs[] = {"get", "1"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(harness_sqlite_query_line(
        db, "UPDATE entries SET expires_at='2020-01-01T00:00:00.000Z' WHERE local_id=1;"));

    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 3);
    ASSERT_STREQ(g.err, "remember: expired\n");
    ASSERT_TRUE(g.out == NULL || g.out[0] == '\0');
    cmd_result_free(&g);
    free(db);
}

void register_get_list_delete_tests(void)
{
    RUN_TEST(get_existing_json_envelope);
    RUN_TEST(get_missing_exits_two);
    RUN_TEST(get_expired_exits_three);
    RUN_TEST(get_human_shows_body);
    RUN_TEST(list_empty_exits_zero);
    RUN_TEST(list_default_order_updated_at_desc);
    RUN_TEST(list_filter_tag_and);
    RUN_TEST(list_filter_source);
    RUN_TEST(list_limit);
    RUN_TEST(list_limit_default_is_twenty);
    RUN_TEST(list_limit_zero_rejected);
    RUN_TEST(list_limit_without_a_value_is_usage);
    RUN_TEST(list_limit_accepts_the_hard_cap);
    RUN_TEST(list_rejects_facet_limit);
    RUN_TEST(list_limit_over_hard_cap_rejected);
    RUN_TEST(list_too_many_tag_filters_rejected);
    RUN_TEST(list_filter_tag_with_quote_is_parameterized);
    RUN_TEST(list_cursor_past_end_keeps_total);
    RUN_TEST(delete_existing);
    RUN_TEST(delete_missing_exits_two);
    RUN_TEST(delete_json_shape);
    RUN_TEST(list_json_cursor_pages);
    RUN_TEST(list_json_cursor_filters_and_rejects);
    RUN_TEST(list_offset_is_an_unknown_option);
    RUN_TEST(json_entry_includes_key_field_null_when_keyless);
}
