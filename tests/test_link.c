#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long add_body(const char *db, const char *body)
{
    const char *a[] = {"add", body};
    CmdResult r;
    long id;

    r = run_remember(db, a, 2, NULL);
    id = (r.exit_code == 0) ? parse_id_stdout(r.out) : -1;
    cmd_result_free(&r);
    return id;
}

static int json_count_is(const char *json, int want)
{
    char needle[32];

    if (json == NULL) {
        return 0;
    }
    (void)snprintf(needle, sizeof(needle), "\"count\":%d", want);
    return strstr(json, needle) != NULL;
}

TEST(link_sugar_related_one_canonical_row)
{
    char *db = make_temp_db_path();
    long a;
    long b;
    const char *link[] = {"--json", "link", "1", "2"};
    const char *again[] = {"--json", "link", "2", "1", "--kind", "related"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    a = add_body(db, "alpha");
    b = add_body(db, "beta");
    ASSERT_EQ_INT(a, 1);
    ASSERT_EQ_INT(b, 2);

    r = run_remember(db, link, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    ASSERT_TRUE(json_count_is(r.out, 1));
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"related\"");
    cmd_result_free(&r);

    r = run_remember(db, again, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"merged\"");
    ASSERT_TRUE(json_count_is(r.out, 1));
    cmd_result_free(&r);

    {
        char *n = harness_sqlite_query_line(db, "SELECT COUNT(*) FROM entry_links;");
        ASSERT_STREQ(n != NULL ? n : "", "1");
        free(n);
        n = harness_sqlite_query_line(
            db, "SELECT from_id || ',' || to_id || ',' || kind FROM entry_links;");
        ASSERT_STREQ(n != NULL ? n : "", "1,2,related");
        free(n);
    }
    free(db);
}

TEST(link_self_missing_kind_cycle)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *self[] = {"link", "1", "1"};
    const char *badkind[] = {"link", "1", "2", "--kind", "cited_by"};
    const char *miss[] = {"link", "--from", "1", "--to", "99"};
    const char *ab[] = {"link", "--from", "1", "--to", "2", "--kind", "supersedes"};
    const char *bc[] = {"link", "--from", "2", "--to", "3", "--kind", "supersedes"};
    const char *ca[] = {"link", "--from", "3", "--to", "1", "--kind", "supersedes"};

    ASSERT_TRUE(db != NULL);
    ASSERT_EQ_INT(add_body(db, "a"), 1);
    ASSERT_EQ_INT(add_body(db, "b"), 2);
    ASSERT_EQ_INT(add_body(db, "c"), 3);

    r = run_remember(db, self, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "self-link");
    cmd_result_free(&r);

    r = run_remember(db, badkind, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);

    r = run_remember(db, miss, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);

    r = run_remember(db, ab, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, bc, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, ca, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "supersedes cycle");
    cmd_result_free(&r);
    free(db);
}

TEST(unlink_idempotent_and_kinds)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *rel[] = {"link", "1", "2"};
    const char *cites[] = {"link", "--from", "1", "--to", "2", "--kind", "cites"};
    const char *noop[] = {"--json", "unlink", "--from", "1", "--to", "2", "--kind", "related"};
    const char *drop_rel[] = {"--json", "unlink", "--from", "2", "--to", "1", "--kind", "related"};
    const char *drop_all[] = {"--json", "unlink", "1", "2"};

    ASSERT_TRUE(db != NULL);
    ASSERT_EQ_INT(add_body(db, "a"), 1);
    ASSERT_EQ_INT(add_body(db, "b"), 2);

    r = run_remember(db, noop, 8, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"deleted\"");
    ASSERT_TRUE(json_count_is(r.out, 0));
    ASSERT_STR_CONTAINS(r.out, "\"links\":[]");
    cmd_result_free(&r);

    r = run_remember(db, rel, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, cites, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, drop_rel, 8, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_count_is(r.out, 1));
    cmd_result_free(&r);
    {
        char *n = harness_sqlite_query_line(db, "SELECT COUNT(*) FROM entry_links;");
        ASSERT_STREQ(n != NULL ? n : "", "1");
        free(n);
        n = harness_sqlite_query_line(db, "SELECT kind FROM entry_links;");
        ASSERT_STREQ(n != NULL ? n : "", "cites");
        free(n);
    }

    r = run_remember(db, drop_all, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_count_is(r.out, 1));
    cmd_result_free(&r);
    {
        char *n = harness_sqlite_query_line(db, "SELECT COUNT(*) FROM entry_links;");
        ASSERT_STREQ(n != NULL ? n : "", "0");
        free(n);
    }
    free(db);
}

TEST(related_json_types_dir_and_trash)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone"};
    const char *rel[] = {"link", "1", "2"};
    const char *cites[] = {"link", "--from", "1", "--to", "3", "--kind", "cites"};
    const char *back[] = {"link", "--from", "3", "--to", "1", "--kind", "cites"};
    const char *q[] = {"--json", "related", "1"};
    const char *outg[] = {"--json", "related", "1", "--outgoing"};
    const char *inc[] = {"--json", "related", "1", "--incoming"};
    const char *both[] = {"related", "1", "--outgoing", "--incoming"};

    ASSERT_TRUE(db != NULL);
    ASSERT_EQ_INT(add_body(db, "keep"), 1);
    r = run_remember(db, exp, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(add_body(db, "citee"), 3);

    r = run_remember(db, rel, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, cites, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, back, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, q, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_count_is(r.out, 3));
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"related\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cited_by\"");
    ASSERT_STR_CONTAINS(r.out, "\"trash\":true");
    cmd_result_free(&r);

    r = run_remember(db, outg, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_count_is(r.out, 2)); /* related + outgoing cites */
    cmd_result_free(&r);
    r = run_remember(db, inc, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_count_is(r.out, 2)); /* related + incoming cites */
    cmd_result_free(&r);

    r = run_remember(db, both, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(link_bumps_both_and_human_id)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *link[] = {"link", "1", "2"};
    char *e1;
    char *e2;
    char *edge;

    ASSERT_TRUE(db != NULL);
    ASSERT_EQ_INT(add_body(db, "a"), 1);
    ASSERT_EQ_INT(add_body(db, "b"), 2);
    r = run_remember(db, link, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(r.out), 1);
    cmd_result_free(&r);
    e1 = harness_sqlite_query_line(db, "SELECT updated_at FROM entries WHERE id=1;");
    e2 = harness_sqlite_query_line(db, "SELECT updated_at FROM entries WHERE id=2;");
    edge = harness_sqlite_query_line(db, "SELECT updated_at FROM entry_links;");
    ASSERT_TRUE(e1 != NULL && e2 != NULL && edge != NULL);
    ASSERT_STREQ(e1, e2);
    ASSERT_STREQ(e1, edge);
    free(e1);
    free(e2);
    free(edge);
    free(db);
}

TEST(rekey_rename_promote_demote_and_errors)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *keyed[] = {"add", "--key", "old:k", "slot"};
    const char *ren[] = {"--json", "rekey", "--key", "old:k", "--to-key", "new:k"};
    const char *prom[] = {"rekey", "2", "--to-key", "b:k"};
    const char *dem[] = {"--json", "rekey", "--key", "new:k", "--clear-key"};
    const char *empty[] = {"rekey", "2", "--to-key", ""};
    const char *both[] = {"rekey", "2", "--to-key", "x", "--clear-key"};
    const char *neither[] = {"rekey", "2"};
    const char *taken[] = {"rekey", "1", "--to-key", "b:k"};
    const char *miss[] = {"rekey", "99", "--to-key", "z"};
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "--key", "t:k", "trash"};
    const char *wrong[] = {"rekey", "--key", "t:k", "--to-key", "t2"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, keyed, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(add_body(db, "other"), 2);

    r = run_remember(db, ren, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"updated\"");
    ASSERT_STR_CONTAINS(r.out, "\"key\":\"new:k\"");
    cmd_result_free(&r);

    r = run_remember(db, prom, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, dem, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"key\":null");
    cmd_result_free(&r);

    r = run_remember(db, empty, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "empty key");
    cmd_result_free(&r);

    r = run_remember(db, both, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, neither, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);

    r = run_remember(db, taken, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "2");
    cmd_result_free(&r);

    r = run_remember(db, miss, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);

    r = run_remember(db, exp, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, wrong, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    cmd_result_free(&r);
    free(db);
}

TEST(link_invalid_id_is_exit_1)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *bad[] = {"link", "abc", "def"};
    const char *emptyk[] = {"link", "--from-key", "", "--to", "1"};

    ASSERT_TRUE(db != NULL);
    ASSERT_EQ_INT(add_body(db, "a"), 1);
    ASSERT_EQ_INT(add_body(db, "b"), 2);

    r = run_remember(db, bad, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "invalid id");
    cmd_result_free(&r);

    r = run_remember(db, emptyk, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "empty key");
    cmd_result_free(&r);
    free(db);
}

static int json_links_after_expires(const char *json)
{
    const char *e;
    const char *l;

    if (json == NULL) {
        return 0;
    }
    e = strstr(json, "\"expires_at\":");
    l = strstr(json, "\"links\":");
    return e != NULL && l != NULL && l > e;
}

static const char *json_links_array(const char *json)
{
    const char *p;

    if (json == NULL) {
        return NULL;
    }
    p = strstr(json, "\"links\":");
    if (p == NULL) {
        return NULL;
    }
    return strchr(p, '[');
}

TEST(list_search_get_json_links_empty_after_expires)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *add[] = {"--json", "add", "solo"};
    const char *lst[] = {"--json", "list"};
    const char *srch[] = {"--json", "search", "solo"};
    const char *get[] = {"--json", "get", "1"};
    const char *upd[] = {"--json", "update", "1", "--text", "solo2"};
    const char *del[] = {"--json", "delete", "1"};
    const char *prg[] = {"--json", "purge-trash"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_NOT_CONTAINS(r.out, "\"links\"");
    cmd_result_free(&r);

    r = run_remember(db, lst, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_links_after_expires(r.out));
    ASSERT_STR_CONTAINS(r.out, "\"links\":[]");
    cmd_result_free(&r);

    r = run_remember(db, srch, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_links_after_expires(r.out));
    ASSERT_STR_CONTAINS(r.out, "\"links\":[]");
    cmd_result_free(&r);

    r = run_remember(db, get, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_links_after_expires(r.out));
    ASSERT_STR_CONTAINS(r.out, "\"links\":[]");
    cmd_result_free(&r);

    r = run_remember(db, upd, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_NOT_CONTAINS(r.out, "\"links\"");
    cmd_result_free(&r);

    r = run_remember(db, del, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_NOT_CONTAINS(r.out, "\"links\"");
    cmd_result_free(&r);

    r = run_remember(db, prg, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_NOT_CONTAINS(r.out, "\"links\"");
    cmd_result_free(&r);
    free(db);
}

TEST(get_list_json_stubs_five_name_preview)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *links_arr;
    char long_body[64];
    const char *add_long[] = {"add", "--key", "slot:k", long_body};
    const char *cites[] = {"link", "--from", "1", "--to", "2", "--kind", "cites"};
    const char *back[] = {"link", "--from", "2", "--to", "1", "--kind", "cites"};
    const char *rel[] = {"link", "1", "3"};
    const char *sup[] = {"link", "--from", "1", "--to", "4", "--kind", "supersedes"};
    const char *by[] = {"link", "--from", "5", "--to", "1", "--kind", "supersedes"};
    const char *get[] = {"--json", "get", "1"};
    const char *lst[] = {"--json", "list", "--key", "hub:k"};
    size_t i;

    ASSERT_TRUE(db != NULL);
    for (i = 0; i < 50U; i++) {
        long_body[i] = 'a';
    }
    long_body[50] = '\0';

    {
        const char *hub[] = {"add", "--key", "hub:k", "hub body unique"};
        r = run_remember(db, hub, 4, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    r = run_remember(db, add_long, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(add_body(db, "related-n"), 3);
    ASSERT_EQ_INT(add_body(db, "old"), 4);
    ASSERT_EQ_INT(add_body(db, "newer"), 5);

    r = run_remember(db, cites, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, back, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, rel, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, sup, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, by, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, get, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_links_after_expires(r.out));
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cited_by\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"related\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"supersedes\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"superseded_by\"");
    ASSERT_STR_CONTAINS(r.out, "\"key\":\"slot:k\"");
    ASSERT_STR_CONTAINS(r.out, "\"preview\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa...\"");
    links_arr = json_links_array(r.out);
    ASSERT_TRUE(links_arr != NULL);
    ASSERT_TRUE(strstr(links_arr, "\"body\"") == NULL);
    cmd_result_free(&r);

    r = run_remember(db, lst, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_links_after_expires(r.out));
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
    links_arr = json_links_array(r.out);
    ASSERT_TRUE(links_arr != NULL);
    ASSERT_TRUE(strstr(links_arr, "\"body\"") == NULL);
    cmd_result_free(&r);
    free(db);
}

TEST(human_list_related_ids_trash_cap)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *hub[] = {"add", "--key", "hub:k", "hub"};
    const char *keyed[] = {"add", "--key", "slot:k", "n2"};
    const char *trash[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone"};
    const char *lst[] = {"list", "--key", "hub:k"};
    int i;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, hub, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, keyed, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(add_body(db, "n3"), 3);
    ASSERT_EQ_INT(add_body(db, "n4"), 4);
    ASSERT_EQ_INT(add_body(db, "n5"), 5);
    ASSERT_EQ_INT(add_body(db, "n6"), 6);
    r = run_remember(db, trash, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    for (i = 2; i <= 7; i++) {
        char from[8];
        char to[8];
        const char *link[3];
        (void)snprintf(from, sizeof(from), "1");
        (void)snprintf(to, sizeof(to), "%d", i);
        link[0] = "link";
        link[1] = from;
        link[2] = to;
        r = run_remember(db, link, 3, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }

    r = run_remember(db, lst, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "7[trash], 6, 5, 4, 3, +1");
    ASSERT_STR_NOT_CONTAINS(r.out, "slot:k");
    cmd_result_free(&r);
    free(db);
}

TEST(human_get_related_block_omitted_when_none)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *get1[] = {"get", "1"};
    const char *get2[] = {"get", "2"};
    const char *link[] = {"link", "--from", "1", "--to", "2", "--kind", "cites"};

    ASSERT_TRUE(db != NULL);
    ASSERT_EQ_INT(add_body(db, "alpha body"), 1);
    ASSERT_EQ_INT(add_body(db, "beta body"), 2);

    r = run_remember(db, get1, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "alpha body");
    ASSERT_STR_NOT_CONTAINS(r.out, "Related:");
    cmd_result_free(&r);

    r = run_remember(db, link, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, get1, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "alpha body");
    ASSERT_STR_CONTAINS(r.out, "Related:");
    ASSERT_STR_CONTAINS(r.out, "cites 2");
    ASSERT_STR_CONTAINS(r.out, "beta body");
    cmd_result_free(&r);

    r = run_remember(db, get2, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "cited_by 1");
    cmd_result_free(&r);
    free(db);
}

TEST(get_marks_trash_neighbor_restore_keeps_edge)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone-n"};
    const char *link[] = {"link", "1", "2"};
    const char *get[] = {"--json", "get", "1"};
    const char *hget[] = {"get", "1"};
    const char *rest[] = {"update", "--trash", "2", "--clear-expires"};

    ASSERT_TRUE(db != NULL);
    ASSERT_EQ_INT(add_body(db, "keep"), 1);
    r = run_remember(db, exp, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, link, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, get, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"trash\":true");
    ASSERT_STR_CONTAINS(r.out, "\"id\":2");
    cmd_result_free(&r);

    r = run_remember(db, hget, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "[trash]");
    cmd_result_free(&r);

    r = run_remember(db, rest, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, get, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"id\":2");
    ASSERT_STR_CONTAINS(r.out, "\"trash\":false");
    cmd_result_free(&r);
    free(db);
}

TEST(help_lists_graph_commands)
{
    char *db = make_temp_db_path();
    const char *h[] = {"--help"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, h, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "link");
    ASSERT_STR_CONTAINS(r.out, "unlink");
    ASSERT_STR_CONTAINS(r.out, "related");
    ASSERT_STR_CONTAINS(r.out, "rekey");
    cmd_result_free(&r);
    free(db);
}

void register_link_tests(void)
{
    RUN_TEST(link_sugar_related_one_canonical_row);
    RUN_TEST(link_self_missing_kind_cycle);
    RUN_TEST(unlink_idempotent_and_kinds);
    RUN_TEST(related_json_types_dir_and_trash);
    RUN_TEST(link_bumps_both_and_human_id);
    RUN_TEST(rekey_rename_promote_demote_and_errors);
    RUN_TEST(link_invalid_id_is_exit_1);
    RUN_TEST(list_search_get_json_links_empty_after_expires);
    RUN_TEST(get_list_json_stubs_five_name_preview);
    RUN_TEST(human_list_related_ids_trash_cap);
    RUN_TEST(human_get_related_block_omitted_when_none);
    RUN_TEST(get_marks_trash_neighbor_restore_keeps_edge);
    RUN_TEST(help_lists_graph_commands);
}
