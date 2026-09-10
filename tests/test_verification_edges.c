#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
/* mkdtemp: Darwin declares it in unistd.h; glibc via stdlib.h + _POSIX_C_SOURCE
 * (remember_apply_posix_feature_macros on Linux). Unconditional unistd breaks
 * Linux clang-tidy include-cleaner; Apple-only matches test_schema_config.c. */
#ifdef __APPLE__
#include <unistd.h>
#endif

/* Timestamp/body buffer sizes for the verification edge tests. */
enum { TS_BUFSIZE = 32, LONG_BODY_LEN = 120, LONG_BODY_BUFSIZE = 200 };

/*
 * Design log Verification Criteria 21–34 and related FTS/GC edges.
 * Still black-box CLI; a few cases use `sqlite3` CLI to inspect schema rows.
 */

/* ---- V21 tag/key normalization extras ------------------------------------ */

TEST(add_tag_casefold_merges_to_one_tag_name)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a1[] = {"add", "--tag", "Foo", "same body casefold"};
    const char *a2[] = {"add", "--tag", "foo", "same body casefold"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "foo");
    ASSERT_STR_NOT_CONTAINS(g.out, "Foo");
    /* single tag element, not two */
    ASSERT_STR_CONTAINS(g.out, "\"tags\":[\"foo\"]");
    cmd_result_free(&g);
    free(db);
}

TEST(add_tag_control_char_rejected)
{
    char *db = make_temp_db_path();
    char tag[] = {'a', 0x01, 'b', '\0'};
    const char *args[] = {"add", "--tag", tag, "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_tag_invalid_utf8_rejected)
{
    char *db = make_temp_db_path();
    char tag[] = {(char)UTF8_INVALID_FF, (char)UTF8_INVALID_FE, 'x', '\0'};
    const char *args[] = {"add", "--tag", tag, "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_key_control_char_rejected)
{
    char *db = make_temp_db_path();
    char key[] = {'k', ASCII_DEL, '\0'};
    const char *args[] = {"add", "--key", key, "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

/* ---- V22 source write only on add ---------------------------------------- */

TEST(update_rejects_source_flag)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    const char *a[] = {"add", "body"};
    const char *uargs[] = {"update", "1", "--source", "agent", "--text", "x"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(get_rejects_source_flag)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "body"};
    const char *gargs[] = {"get", "--source", "human", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 1);
    cmd_result_free(&g);
    free(db);
}

TEST(delete_rejects_source_flag)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    const char *a[] = {"add", "body"};
    const char *dargs[] = {"delete", "--source", "tool", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, sizeof(dargs) / sizeof(dargs[0]), NULL);
    ASSERT_EQ_INT(d.exit_code, 1);
    cmd_result_free(&d);
    free(db);
}

TEST(keyless_merge_keeps_created_at)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g1;
    CmdResult g2;
    const char *a1[] = {"add", "--json", "--source", "human", "stable created"};
    const char *a2[] = {"add", "--json", "--tag", "t", "stable created"};
    const char *gargs[] = {"get", "--json", "1"};
    char *created1 = NULL;
    const char *p = NULL;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g1 = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g1.exit_code, 0);
    p = (g1.out != NULL) ? strstr(g1.out, "\"created_at\":\"") : NULL;
    ASSERT_TRUE(p != NULL);
    if (p != NULL) {
        const char *end = NULL;
        p += strlen("\"created_at\":\"");
        end = strchr(p, '"');
        created1 = (char *)malloc(TS_BUFSIZE);
        ASSERT_TRUE(created1 != NULL);
        if (created1 != NULL) {
            created1[0] = '\0';
            if (end != NULL) {
                (void)snprintf(created1, 32, "%.*s", (int)(end - p), p);
            }
        }
    }
    cmd_result_free(&g1);
    if (created1 == NULL || created1[0] == '\0') {
        free(created1);
        free(db);
        return;
    }
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g2 = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g2.exit_code, 0);
    ASSERT_STR_CONTAINS(g2.out, created1);
    ASSERT_STR_CONTAINS(g2.out, "\"source\":\"human\"");
    cmd_result_free(&g2);
    free(created1);
    free(db);
}

/* ---- V23 update invariants ----------------------------------------------- */

TEST(update_preserves_id_key_source_created_at)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult g;
    const char *a[] = {"add", "--json", "--key", "slot", "--source", "human", "v1"};
    const char *uargs[] = {"update", "--json", "--key", "slot", "--text", "v2"};
    const char *gargs[] = {"get", "--json", "--key", "slot"};
    char created[TS_BUFSIZE];
    const char *p = NULL;
    ASSERT_TRUE(db != NULL);
    created[0] = '\0';
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    p = (r.out != NULL) ? strstr(r.out, "\"created_at\":\"") : NULL;
    ASSERT_TRUE(p != NULL);
    if (p != NULL) {
        const char *end = NULL;
        p += strlen("\"created_at\":\"");
        end = strchr(p, '"');
        if (end != NULL) {
            (void)snprintf(created, sizeof(created), "%.*s", (int)(end - p), p);
        }
    }
    cmd_result_free(&r);
    if (created[0] == '\0') {
        free(db);
        return;
    }
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    ASSERT_STR_CONTAINS(u.out, "\"id\":1");
    ASSERT_STR_CONTAINS(u.out, "\"key\":\"slot\"");
    ASSERT_STR_CONTAINS(u.out, "\"source\":\"human\"");
    ASSERT_STR_CONTAINS(u.out, created);
    ASSERT_STR_CONTAINS(u.out, "v2");
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, created);
    cmd_result_free(&g);
    free(db);
}

/* ---- V24 orphan-tag GC --------------------------------------------------- */

TEST(orphan_tag_removed_when_last_use_deleted)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    char *count = NULL;
    const char *a[] = {"add", "--tag", "orphanonly", "solo"};
    const char *dargs[] = {"delete", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    d = run_remember(db, dargs, sizeof(dargs) / sizeof(dargs[0]), NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    cmd_result_free(&d);
    count = harness_sqlite_query_line(db, "SELECT count(*) FROM tags WHERE name='orphanonly';");
    ASSERT_TRUE(count != NULL);
    ASSERT_STREQ(count, "0");
    free(count);
    free(db);
}

TEST(shared_tag_survives_when_other_entry_uses_it)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    char *count = NULL;
    const char *a1[] = {"add", "--tag", "shared", "one"};
    const char *a2[] = {"add", "--tag", "shared", "two"};
    const char *dargs[] = {"delete", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, sizeof(dargs) / sizeof(dargs[0]), NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    cmd_result_free(&d);
    count = harness_sqlite_query_line(db, "SELECT count(*) FROM tags WHERE name='shared';");
    ASSERT_TRUE(count != NULL);
    ASSERT_STREQ(count, "1");
    free(count);
    free(db);
}

/* ---- V25 FTS sync -------------------------------------------------------- */

TEST(fts_search_reflects_body_update)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult s;
    const char *a[] = {"add", "oldtokenxyz unique body"};
    const char *uargs[] = {"update", "1", "--text", "newtokenabc unique body"};
    const char *sold[] = {"search", "--json", "oldtokenxyz"};
    const char *snew[] = {"search", "--json", "newtokenabc"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    s = run_remember(db, sold, sizeof(sold) / sizeof(sold[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "\"total\":0");
    cmd_result_free(&s);
    s = run_remember(db, snew, sizeof(snew) / sizeof(snew[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "newtokenabc");
    cmd_result_free(&s);
    free(db);
}

TEST(fts_search_empty_after_delete)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    CmdResult s;
    const char *a[] = {"add", "deleteftsunique99"};
    const char *dargs[] = {"delete", "1"};
    const char *sargs[] = {"search", "--json", "deleteftsunique99"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, sizeof(dargs) / sizeof(dargs[0]), NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    cmd_result_free(&d);
    s = run_remember(db, sargs, sizeof(sargs) / sizeof(sargs[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "\"total\":0");
    cmd_result_free(&s);
    free(db);
}

TEST(fts_search_reflects_keyed_upsert)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult s;
    const char *a1[] = {"add", "--key", "k", "firstval_unique"};
    const char *a2[] = {"add", "--key", "k", "secondval_unique"};
    const char *s1[] = {"search", "--json", "firstval_unique"};
    const char *s2[] = {"search", "--json", "secondval_unique"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    s = run_remember(db, s1, sizeof(s1) / sizeof(s1[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "\"total\":0");
    cmd_result_free(&s);
    s = run_remember(db, s2, sizeof(s2) / sizeof(s2[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "secondval_unique");
    cmd_result_free(&s);
    free(db);
}

/* ---- V26 compound tag + diacritics ---------------------------------------- */

TEST(search_finds_compound_tag_component)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult s;
    const char *a[] = {"add", "--tag", "pref:editor", "uses helix editor maybe"};
    const char *sargs[] = {"search", "--json", "editor"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    s = run_remember(db, sargs, sizeof(sargs) / sizeof(sargs[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "helix");
    cmd_result_free(&s);
    free(db);
}

TEST(search_diacritics_folded)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult s;
    const char *a[] = {"add", "I love café culture"};
    const char *sargs[] = {"search", "--json", "cafe"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    s = run_remember(db, sargs, sizeof(sargs) / sizeof(sargs[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "caf");
    cmd_result_free(&s);
    free(db);
}

/* ---- V27 invalid FTS (unbalanced quote) ---------------------------------- */

TEST(search_unbalanced_quote_rejected)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult s;
    const char *a[] = {"add", "quote test body"};
    const char *sargs[] = {"search", "\"unbalanced"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    s = run_remember(db, sargs, sizeof(sargs) / sizeof(sargs[0]), NULL);
    ASSERT_EQ_INT(s.exit_code, 1);
    ASSERT_TRUE(!str_is_blank(s.err));
    cmd_result_free(&s);
    free(db);
}

/* ---- V28 empty list success JSON ----------------------------------------- */

TEST(list_no_matches_json_total_zero)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult l;
    const char *a[] = {"add", "--tag", "x", "only"};
    const char *largs[] = {"list", "--json", "--tag", "nomatch"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    l = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(l.exit_code, 0);
    ASSERT_STR_CONTAINS(l.out, "\"entries\":[]");
    ASSERT_STR_CONTAINS(l.out, "\"total\":0");
    cmd_result_free(&l);
    free(db);
}

/* ---- V29/V30 stdin + invalid UTF-8 body ---------------------------------- */

TEST(add_invalid_utf8_body_rejected)
{
    char *db = make_temp_db_path();
    const char bad[] = {(char)UTF8_STRAY_CONT, (char)UTF8_INVALID_FF, 'a', '\0'};
    const char *args[] = {"add", "-"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), bad);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(update_invalid_utf8_text_rejected)
{
    char *db = make_temp_db_path();
    const char bad[] = {(char)UTF8_INVALID_FF, '\0'};
    CmdResult r;
    CmdResult u;
    const char *a[] = {"add", "ok"};
    const char *uargs[] = {"update", "1", "--text", "-"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), bad);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

/* ---- V32 preview edges --------------------------------------------------- */

TEST(human_list_preview_first_line_only)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult l;
    const char *a[] = {"add", "firstline_unique\nsecondline_secret_should_hide"};
    const char *largs[] = {"list"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    l = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(l.exit_code, 0);
    ASSERT_STR_CONTAINS(l.out, "firstline_unique");
    ASSERT_STR_NOT_CONTAINS(l.out, "secondline_secret_should_hide");
    cmd_result_free(&l);
    free(db);
}

TEST(human_list_preview_truncates_long_line)
{
    char *db = make_temp_db_path();
    char body[LONG_BODY_BUFSIZE];
    CmdResult r;
    CmdResult l;
    const char *a[2];
    const char *largs[] = {"list"};
    size_t i = 0;
    ASSERT_TRUE(db != NULL);
    for (i = 0; i < LONG_BODY_LEN; i++) {
        body[i] = 'x';
    }
    body[LONG_BODY_LEN] = '\0';
    a[0] = "add";
    a[1] = body;
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    l = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(l.exit_code, 0);
    /* full 120 x's should not all appear in one preview field */
    ASSERT_TRUE(strstr(l.out, body) == NULL);
    ASSERT_STR_CONTAINS(l.out, "xxx");
    cmd_result_free(&l);
    free(db);
}

/* ---- V33 dir mode 0700 --------------------------------------------------- */

TEST(db_parent_dir_mode_0700)
{
    /* remember must create the missing parent dir as 0700 (not pre-created by harness). */
    char tmpl[] = "/tmp/remember-mkdir-XXXXXX";
    const char *base = NULL;
    char *db = NULL;
    char *created_dir = NULL;
    CmdResult r;
    const char *a[] = {"add", "perms"};
    struct stat st;
    size_t n = 0;
    base = mkdtemp(tmpl);
    ASSERT_TRUE(base != NULL);
    if (base == NULL) {
        return;
    }
    n = strlen(base) + strlen("/nested/test.db") + 1U;
    db = (char *)malloc(n);
    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    (void)snprintf(db, n, "%s/nested/test.db", base);
    n = strlen(base) + strlen("/nested") + 1U;
    created_dir = (char *)malloc(n);
    ASSERT_TRUE(created_dir != NULL);
    if (created_dir == NULL) {
        free(db);
        return;
    }
    (void)snprintf(created_dir, n, "%s/nested", base);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(stat(created_dir, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & (unsigned)PERM_BITS_MASK), DIR_PERMS);
    free(created_dir);
    free(db);
}

/* ---- V14 user_version too new -------------------------------------------- */

TEST(schema_user_version_too_new_refused)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult l;
    char *ver = NULL;
    const char *a[] = {"add", "bootstrap"};
    const char *largs[] = {"list"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    /* Bump schema beyond tool support (reuse the query helper; set returns no rows). */
    free(harness_sqlite_query_line(db, "PRAGMA user_version=99;"));
    ver = harness_sqlite_query_line(db, "PRAGMA user_version;");
    ASSERT_TRUE(ver != NULL);
    ASSERT_STREQ(ver, "99");
    free(ver);
    l = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(l.exit_code, 1);
    ASSERT_TRUE(!str_is_blank(l.err));
    cmd_result_free(&l);
    free(db);
}

/* ---- V34 errors under --json --------------------------------------------- */

TEST(json_get_missing_empty_stdout)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "x"};
    const char *gargs[] = {"get", "--json", "99"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    ASSERT_TRUE(str_is_blank(g.out));
    ASSERT_TRUE(!str_is_blank(g.err));
    ASSERT_STR_NOT_CONTAINS(g.err, "\"version\"");
    cmd_result_free(&g);
    free(db);
}

TEST(json_add_error_empty_stdout)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *args[] = {"add", "--json", ""};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_TRUE(str_is_blank(r.out));
    ASSERT_TRUE(!str_is_blank(r.err));
    cmd_result_free(&r);
    free(db);
}

/*
 * Green verification criteria through step 07 (add/get/list/delete/update edges).
 * Suite name `verification_gld` kept for historical continuity.
 */
void register_verification_gld_tests(void)
{
    RUN_TEST(add_tag_casefold_merges_to_one_tag_name);
    RUN_TEST(add_tag_control_char_rejected);
    RUN_TEST(add_tag_invalid_utf8_rejected);
    RUN_TEST(add_key_control_char_rejected);
    RUN_TEST(keyless_merge_keeps_created_at);
    RUN_TEST(add_invalid_utf8_body_rejected);
    RUN_TEST(db_parent_dir_mode_0700);
    RUN_TEST(json_add_error_empty_stdout);
    RUN_TEST(get_rejects_source_flag);
    RUN_TEST(delete_rejects_source_flag);
    RUN_TEST(update_rejects_source_flag);
    RUN_TEST(update_preserves_id_key_source_created_at);
    RUN_TEST(update_invalid_utf8_text_rejected);
    RUN_TEST(schema_user_version_too_new_refused);
    RUN_TEST(json_get_missing_empty_stdout);
    RUN_TEST(human_list_preview_first_line_only);
    RUN_TEST(human_list_preview_truncates_long_line);
    RUN_TEST(orphan_tag_removed_when_last_use_deleted);
    RUN_TEST(shared_tag_survives_when_other_entry_uses_it);
    RUN_TEST(list_no_matches_json_total_zero);
}

/*
 * Green FTS / search verification through step 07 (includes body-update resync).
 */
void register_verification_search_tests(void)
{
    RUN_TEST(fts_search_empty_after_delete);
    RUN_TEST(fts_search_reflects_keyed_upsert);
    RUN_TEST(fts_search_reflects_body_update);
    RUN_TEST(search_finds_compound_tag_component);
    RUN_TEST(search_diacritics_folded);
    RUN_TEST(search_unbalanced_quote_rejected);
}

void register_verification_edges_tests(void)
{
    /* Full edges suite once later-step reds have been promoted. */
    register_verification_gld_tests();
    register_verification_search_tests();
}
