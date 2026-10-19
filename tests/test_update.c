#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

enum { TS_BUFSIZE = 32, SYNC_ID_CAP = 40, VV_CAP = 128 }; /* stamp, UUID, version vector */

/* Copy the value of a `"key":"..."` JSON string field into out (empty if absent). */
static void extract_json_str(const char *json, const char *key, char *out, size_t outsz)
{
    const char *p = NULL;
    const char *end = NULL;
    size_t len = 0;

    out[0] = '\0';
    p = (json != NULL) ? strstr(json, key) : NULL;
    if (p == NULL) {
        return;
    }
    p += strlen(key);
    end = strchr(p, '"');
    if (end == NULL) {
        return;
    }
    len = (size_t)(end - p);
    if (len >= outsz) {
        len = outsz - 1U;
    }
    memcpy(out, p, len);
    out[len] = '\0';
}

/* Copy the raw `"version_vector":{...}` object. One level, no nested braces. */
static void extract_version_vector(const char *json, char *out, size_t outsz)
{
    const char *key = "\"version_vector\":";
    const char *p = NULL;
    const char *end = NULL;
    size_t len = 0;

    out[0] = '\0';
    p = (json != NULL) ? strstr(json, key) : NULL;
    if (p == NULL) {
        return;
    }
    p += strlen(key);
    if (p[0] != '{') {
        return;
    }
    end = strchr(p, '}');
    if (end == NULL) {
        return;
    }
    end++;
    len = (size_t)(end - p);
    if (len >= outsz) {
        return;
    }
    memcpy(out, p, len);
    out[len] = '\0';
}

static void seed_tagged(const char *db)
{
    const char *args[] = {"add", "--tag", "a", "--tag", "b", "--source", "human", "original body"};
    CmdResult r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    cmd_result_free(&r);
}

TEST(update_text_only_keeps_tags)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--text", "new body only"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(u.out), 1);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "new body only");
    ASSERT_STR_CONTAINS(g.out, "a");
    ASSERT_STR_CONTAINS(g.out, "b");
    cmd_result_free(&g);
    free(db);
}

TEST(update_tags_only_keeps_body)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--tag", "z"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "original body");
    ASSERT_STR_CONTAINS(g.out, "z");
    ASSERT_STR_NOT_CONTAINS(g.out, "\"a\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_clear_tags_flag)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--clear-tags"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "original body");
    ASSERT_STR_CONTAINS(g.out, "\"tags\":[]");
    cmd_result_free(&g);
    free(db);
}

TEST(update_tag_and_clear_tags_rejected)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "1", "--tag", "x", "--clear-tags"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(update_same_text_still_succeeds_and_returns_entry)
{
    /* Successful update always refreshes updated_at (writing is a touch). */
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "--json", "1", "--text", "original body"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    ASSERT_STR_CONTAINS(u.out, "\"action\":\"updated\"");
    ASSERT_STR_CONTAINS(u.out, "\"entries\"");
    ASSERT_STR_CONTAINS(u.out, "original body");
    cmd_result_free(&u);
    free(db);
}

TEST(update_text_and_tags_together)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--text", "both changed", "--tag", "only"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "both changed");
    ASSERT_STR_CONTAINS(g.out, "only");
    ASSERT_STR_NOT_CONTAINS(g.out, "\"a\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_no_change_rejected)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(update_missing_id_exits_two)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "99", "--text", "nope"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 2);
    cmd_result_free(&u);
    free(db);
}

TEST(update_body_hash_collision_rejected)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    const char *a1[] = {"add", "body one"};
    const char *a2[] = {"add", "body two"};
    const char *uargs[] = {"update", "1", "--text", "body two"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    ASSERT_STR_CONTAINS(u.err, "2");
    cmd_result_free(&u);
    free(db);
}

TEST(update_empty_text_rejected)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "1", "--text", "   "};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(update_source_immutable)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--text", "still human source"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"source\":\"human\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_json_shape)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "--json", "1", "--text", "json update"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    ASSERT_STR_CONTAINS(u.out, "\"version\":1");
    ASSERT_STR_CONTAINS(u.out, "\"action\":\"updated\"");
    ASSERT_STR_CONTAINS(u.out, "\"count\":1");
    ASSERT_STR_CONTAINS(u.out, "\"entries\"");
    ASSERT_STR_CONTAINS(u.out, "json update");
    cmd_result_free(&u);
    free(db);
}

TEST(update_text_stdin_dash)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--text", "-"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), "stdin update body");
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "stdin update body");
    cmd_result_free(&g);
    free(db);
}

/* `--text=-` stores a literal hyphen body (not stdin). */
TEST(update_text_literal_dash_equals_form)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--text=-"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"body\":\"-\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_positional_body_not_accepted)
{
    /* Regression: positional body on update must not work (tag wipe footgun). */
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "positional not allowed"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "original body");
    ASSERT_STR_CONTAINS(g.out, "a");
    cmd_result_free(&g);
    free(db);
}

TEST(update_replace_tags_multiple)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--tag", "one", "--tag", "two"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "one");
    ASSERT_STR_CONTAINS(g.out, "two");
    ASSERT_STR_NOT_CONTAINS(g.out, "\"a\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_replace_tags_keeps_shared_tag_on_other_entry)
{
    /* Replacing entry 1's tags must not GC a tag entry 2 still uses. */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult g;
    const char *a1[] = {"add", "--tag", "shared", "--tag", "x", "e1"};
    const char *a2[] = {"add", "--tag", "shared", "--tag", "y", "e2"};
    const char *uargs[] = {"update", "1", "--tag", "only"};
    const char *gargs[] = {"get", "--json", "2"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "shared");
    ASSERT_STR_CONTAINS(g.out, "y");
    cmd_result_free(&g);
    free(db);
}

TEST(update_by_key_clear_tags)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult g;
    const char *a[] = {"add", "--key", "k", "--tag", "t", "body"};
    const char *uargs[] = {"update", "--key", "k", "--clear-tags"};
    const char *gargs[] = {"get", "--json", "--key", "k"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, sizeof(gargs) / sizeof(gargs[0]), NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"tags\":[]");
    cmd_result_free(&g);
    free(db);
}

TEST(fts_reflects_tag_update)
{
    /* Tag replace re-syncs FTS: old tag token drops, new one is findable. */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult s;
    const char *a[] = {"add", "--tag", "oldtagxyz", "plain body"};
    const char *uargs[] = {"update", "1", "--tag", "newtagabc"};
    const char *sold[] = {"search", "--json", "oldtagxyz"};
    const char *snew[] = {"search", "--json", "newtagabc"};
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
    ASSERT_STR_CONTAINS(s.out, "plain body");
    cmd_result_free(&s);
    free(db);
}

TEST(update_moves_entry_to_top_of_list)
{
    /* Regression: sub-second updated_at so a just-updated older entry sorts
       ahead of a newer one added in the same second (newest-first). */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult l;
    const char *a1[] = {"add", "first"};
    const char *a2[] = {"add", "second"};
    const char *uargs[] = {"update", "1", "--text", "first edited"};
    const char *largs[] = {"list", "--json"};
    const char *p1 = NULL;
    const char *p2 = NULL;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    l = run_remember(db, largs, sizeof(largs) / sizeof(largs[0]), NULL);
    ASSERT_EQ_INT(l.exit_code, 0);
    p1 = (l.out != NULL) ? strstr(l.out, "\"id\":1") : NULL;
    p2 = (l.out != NULL) ? strstr(l.out, "\"id\":2") : NULL;
    ASSERT_TRUE(p1 != NULL);
    ASSERT_TRUE(p2 != NULL);
    ASSERT_TRUE(p1 < p2); /* updated id 1 precedes untouched id 2 */
    cmd_result_free(&l);
    free(db);
}

TEST(update_bumps_updated_at)
{
    /* A successful update always advances updated_at (sub-second resolution
       makes the change observable without sleeping). */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    char before[TS_BUFSIZE];
    char after[TS_BUFSIZE];
    const char *a[] = {"add", "--json", "orig body"};
    const char *uargs[] = {"update", "--json", "1", "--text", "changed body"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    extract_json_str(r.out, "\"updated_at\":\"", before, sizeof(before));
    cmd_result_free(&r);
    ASSERT_TRUE(before[0] != '\0');
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    extract_json_str(u.out, "\"updated_at\":\"", after, sizeof(after));
    ASSERT_TRUE(after[0] != '\0');
    ASSERT_TRUE(strcmp(before, after) != 0);
    cmd_result_free(&u);
    free(db);
}

TEST(update_if_version_vector_rejects_malformed)
{
    char *db = make_temp_db_path();
    const char *args[] = {"update", "1", "--if-version-vector", "not-a-vector", "--text", "new"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    seed_tagged(db);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: invalid version vector: not-a-vector\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

static void given_version_vector(const char *db, char *vector, size_t vector_cap)
{
    const char *get[] = {"get", "--json", "1"};
    CmdResult r = run_remember(db, get, sizeof(get) / sizeof(get[0]), NULL);

    vector[0] = '\0';
    ASSERT_EQ_INT(r.exit_code, 0);
    extract_version_vector(r.out, vector, vector_cap);
    ASSERT_TRUE(vector[0] == '{');
    cmd_result_free(&r);
}

TEST(update_if_version_vector_match_writes)
{
    char *db = make_temp_db_path();
    char vector[VV_CAP];
    CmdResult got;
    const char *add[] = {"add", "original body"};

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    got = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    cmd_result_free(&got);
    given_version_vector(db, vector, sizeof(vector));
    if (vector[0] == '\0') {
        free(db);
        return;
    }
    {
        const char *args[] = {"update", "1", "--if-version-vector", vector, "--text", "new body"};
        CmdResult updated = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);

        ASSERT_EQ_INT(updated.exit_code, 0);
        ASSERT_STREQ(updated.err, "");
        cmd_result_free(&updated);
    }
    free(db);
}

TEST(update_if_version_vector_mismatch_is_stale)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json-errors",        "update", "1",       "--if-version-vector",
                          "{\"other-device\":1}", "--text", "new body"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    seed_tagged(db);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "{\"version\":1,\"error\":{\"code\":\"stale\",\"message\":\"memory "
                        "changed since it was read\"}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(update_if_version_vector_rejects_a_duplicate)
{
    char *db = make_temp_db_path();
    const char *args[] = {
        "update", "1",  "--if-version-vector", "{\"a\":1}", "--if-version-vector", "{\"a\":2}",
        "--text", "new"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    seed_tagged(db);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: duplicate --if-version-vector\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

TEST(update_if_version_vector_rejects_a_missing_value)
{
    char *db = make_temp_db_path();
    const char *args[] = {"update", "1", "--if-version-vector"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    seed_tagged(db);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: missing value for --if-version-vector\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

TEST(update_if_version_vector_alone_is_not_a_change)
{
    char *db = make_temp_db_path();
    const char *args[] = {"update", "1", "--if-version-vector", "{\"a\":1}"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    seed_tagged(db);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: update requires --text, --tag, --clear-tags, --ttl, "
                        "--expires, --clear-expires, or --undelete\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

TEST(update_if_version_vector_match_by_key_writes)
{
    char *db = make_temp_db_path();
    char vector[VV_CAP];
    CmdResult got;
    const char *add[] = {"add", "--key", "slot", "original body"};

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    got = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    cmd_result_free(&got);
    given_version_vector(db, vector, sizeof(vector));
    if (vector[0] == '\0') {
        free(db);
        return;
    }
    {
        const char *args[] = {"update", "--key",  "slot",    "--if-version-vector",
                              vector,   "--text", "new body"};
        CmdResult updated = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);

        ASSERT_EQ_INT(updated.exit_code, 0);
        ASSERT_STREQ(updated.err, "");
        cmd_result_free(&updated);
    }
    free(db);
}

TEST(update_if_version_vector_match_by_sync_id_writes)
{
    char *db = make_temp_db_path();
    char vector[VV_CAP];
    char sync_id[SYNC_ID_CAP];
    CmdResult got;
    const char *add[] = {"add", "--json", "original body"};

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    got = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 0);
    extract_version_vector(got.out, vector, sizeof(vector));
    extract_json_str(got.out, "\"sync_id\":\"", sync_id, sizeof(sync_id));
    cmd_result_free(&got);
    if (vector[0] == '\0' || sync_id[0] == '\0') {
        free(db);
        return;
    }
    {
        const char *args[] = {"update", "--sync-id", sync_id,   "--if-version-vector",
                              vector,   "--text",    "new body"};
        CmdResult updated = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);

        ASSERT_EQ_INT(updated.exit_code, 0);
        ASSERT_STREQ(updated.err, "");
        cmd_result_free(&updated);
    }
    free(db);
}

TEST(update_if_version_vector_undelete_match_restores)
{
    char *db = make_temp_db_path();
    char vector[VV_CAP];
    CmdResult got;
    const char *add[] = {"add", "original body"};
    const char *del[] = {"delete", "1"};
    const char *deleted[] = {"get", "--deleted", "--json", "1"};
    const char *live[] = {"get", "--json", "1"};

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    got = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    cmd_result_free(&got);
    got = run_remember(db, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = run_remember(db, deleted, sizeof(deleted) / sizeof(deleted[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 0);
    extract_version_vector(got.out, vector, sizeof(vector));
    cmd_result_free(&got);
    if (vector[0] == '\0') {
        free(db);
        return;
    }
    {
        const char *args[] = {"update", "1", "--deleted", "--undelete", "--if-version-vector",
                              vector};
        CmdResult updated = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);

        ASSERT_EQ_INT(updated.exit_code, 0);
        ASSERT_STREQ(updated.err, "");
        cmd_result_free(&updated);
    }
    got = run_remember(db, live, sizeof(live) / sizeof(live[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STR_CONTAINS(got.out, "original body");
    ASSERT_STR_CONTAINS(got.out, "\"bin\":\"live\"");
    cmd_result_free(&got);
    free(db);
}

TEST(update_help_names_the_version_vector_guard)
{
    char *db = make_temp_db_path();
    const char *args[] = {"help", "update"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    if (r.exit_code != 0) {
        cmd_result_free(&r);
        free(db);
        return;
    }
    ASSERT_STR_CONTAINS(r.out, "  --if-version-vector VV\n"
                               "                  Write only when version_vector equals VV\n");
    ASSERT_STR_CONTAINS(r.out, "A version-vector mismatch is code stale "
                               "(memory changed since it was read) and writes nothing.\n");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(db);
}

void register_update_tests(void)
{
    RUN_TEST(update_text_only_keeps_tags);
    RUN_TEST(update_tags_only_keeps_body);
    RUN_TEST(update_clear_tags_flag);
    RUN_TEST(update_tag_and_clear_tags_rejected);
    RUN_TEST(update_same_text_still_succeeds_and_returns_entry);
    RUN_TEST(update_text_and_tags_together);
    RUN_TEST(update_no_change_rejected);
    RUN_TEST(update_missing_id_exits_two);
    RUN_TEST(update_body_hash_collision_rejected);
    RUN_TEST(update_empty_text_rejected);
    RUN_TEST(update_source_immutable);
    RUN_TEST(update_json_shape);
    RUN_TEST(update_text_stdin_dash);
    RUN_TEST(update_text_literal_dash_equals_form);
    RUN_TEST(update_positional_body_not_accepted);
    RUN_TEST(update_replace_tags_multiple);
    RUN_TEST(update_replace_tags_keeps_shared_tag_on_other_entry);
    RUN_TEST(update_by_key_clear_tags);
    RUN_TEST(fts_reflects_tag_update);
    RUN_TEST(update_moves_entry_to_top_of_list);
    RUN_TEST(update_bumps_updated_at);
    RUN_TEST(update_if_version_vector_rejects_malformed);
    RUN_TEST(update_if_version_vector_match_writes);
    RUN_TEST(update_if_version_vector_mismatch_is_stale);
    RUN_TEST(update_if_version_vector_rejects_a_duplicate);
    RUN_TEST(update_if_version_vector_rejects_a_missing_value);
    RUN_TEST(update_if_version_vector_alone_is_not_a_change);
    RUN_TEST(update_if_version_vector_match_by_key_writes);
    RUN_TEST(update_if_version_vector_match_by_sync_id_writes);
    RUN_TEST(update_if_version_vector_undelete_match_restores);
    RUN_TEST(update_help_names_the_version_vector_guard);
}
