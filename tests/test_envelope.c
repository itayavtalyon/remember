#ifdef __linux__
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) glibc
                    // feature macro
#endif

#include "appio.h"
#include "cli.h"
#include "diag.h"
#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "remember_app.h"
#include "store.h"
#include "test.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h> /* mkdtemp on Darwin; access */

/* One memory with this key and the body "a body"; it is number 1 on a new brain. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void given_one_memory_with_key(const char *db, const char *key)
{
    const char *add[] = {"add", "--key", key, "a body"};
    CmdResult r = run_remember(db, add, sizeof add / sizeof add[0], NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

TEST(envelope_get_head_is_format_version_type_then_data_memory)
{
    char *db = make_temp_db_path();
    const char *get[] = {"--json", "get", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,"
                             "\"type\":\"memory.get.result\",\"data\":{\"memory\":{");
    cmd_result_free(&r);
    free(db);
}

/* Memory 1 (key hub) is linked to memory 2, and memory 2 is in the Trash. */
static void given_hub_linked_to_a_deleted_memory(const char *db)
{
    const char *const steps[][4] = {{"add", "--key", "hub", "hub body"},
                                    {"add", "other body", NULL, NULL},
                                    {"link", "1", "2", NULL},
                                    {"delete", "2", NULL, NULL}};
    const size_t lens[] = {4, 2, 3, 2};
    size_t i = 0;

    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        CmdResult r = run_remember(db, steps[i], lens[i], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
}

TEST(envelope_add_leaves_out_a_neighbor_in_the_trash_like_get)
{
    char *db = make_temp_db_path();
    const char *add[] = {"--json", "add", "--key", "hub", "new hub body"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_hub_linked_to_a_deleted_memory(db);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"version_vector\":{");
    ASSERT_STR_CONTAINS(r.out, "\"links\":[]}}}\n");
    cmd_result_free(&r);
    free(db);
}

/* A person at the CLI is human with a null id. The memory has no source. */
TEST(envelope_a_new_memory_has_created_by_and_updated_by_and_no_source)
{
    char *db = make_temp_db_path();
    const char *add[] = {"--json", "add", "a body"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
                               "\"updated_by\":{\"kind\":\"human\",\"id\":null},\"created_at\":");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"source\"");
    cmd_result_free(&r);
    free(db);
}

enum { TYPE_STEP_ARGS = 7, TYPE_PREFIX_CAP = 128 };

/* One command in order on one brain, and the reply type its envelope names. */
typedef struct {
    const char *args[TYPE_STEP_ARGS];
    const char *type;
} TypeStep;

enum { ENVELOPE_KEYS_MAX = 8, ENVELOPE_KEY_CAP = 16 };

/* The top-level keys of one JSON object, in order. Strings may hold braces and
   escaped quotes; only depth-1 keys count. Returns how many, or -1 if malformed. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) one small JSON scanner
static int envelope_top_keys(const char *json, char keys[][ENVELOPE_KEY_CAP])
{
    int depth = 0;
    int n = 0;
    bool in_str = false;
    bool want_key = false;
    const char *p = json;
    const char *start = NULL;

    for (; *p != '\0'; p++) {
        if (in_str) {
            if (*p == '\\') {
                p++;
                continue;
            }
            if (*p != '"') {
                continue;
            }
            in_str = false;
            if (start != NULL) {
                if (n >= ENVELOPE_KEYS_MAX || p - start >= ENVELOPE_KEY_CAP) {
                    return -1;
                }
                (void)snprintf(keys[n++], ENVELOPE_KEY_CAP, "%.*s", (int)(p - start), start);
                start = NULL;
            }
        } else if (*p == '"') {
            in_str = true;
            start = (depth == 1 && want_key) ? p + 1 : NULL;
            want_key = false;
        } else if (*p == '{' || *p == '[') {
            depth++;
            want_key = depth == 1;
        } else if (*p == '}' || *p == ']') {
            depth--;
        } else if (*p == ',' && depth == 1) {
            want_key = true;
        }
    }
    return depth == 0 ? n : -1;
}

/* 008-1 / 008-2: format, version, type, then data or error, then only
   next_cursor, has_more (with data), and warnings, in that order. No envelope
   id, sync_id, source, or offset, and no "type":"error" anywhere. */
static void assert_envelope_shape(const char *out)
{
    static const char *const tail[] = {"next_cursor", "has_more", "warnings"};
    char keys[ENVELOPE_KEYS_MAX][ENVELOPE_KEY_CAP];
    int n = envelope_top_keys(out, keys);
    int i = 0;
    size_t t = 0;
    bool data = false;

    ASSERT_TRUE(n >= 4);
    if (n < 4) {
        return;
    }
    ASSERT_STREQ(keys[0], "format");
    ASSERT_STREQ(keys[1], "version");
    ASSERT_STREQ(keys[2], "type");
    data = strcmp(keys[3], "data") == 0;
    ASSERT_TRUE(data || strcmp(keys[3], "error") == 0);
    for (i = 4; i < n; i++) {
        while (t < sizeof tail / sizeof tail[0] && strcmp(keys[i], tail[t]) != 0) {
            t++;
        }
        ASSERT_TRUE(t < sizeof tail / sizeof tail[0]);
        ASSERT_TRUE(data || t == 2U);
        t++;
    }
    ASSERT_STR_NOT_CONTAINS(out, "\"type\":\"error\"");
    ASSERT_STR_NOT_CONTAINS(out, "\"sync_id\"");
    ASSERT_STR_NOT_CONTAINS(out, "\"source\"");
    ASSERT_STR_NOT_CONTAINS(out, "\"offset\"");
}

/* The checker itself: a misordered or unknown top-level key fails it. */
TEST(envelope_shape_checker_reads_only_top_level_keys)
{
    char keys[ENVELOPE_KEYS_MAX][ENVELOPE_KEY_CAP];

    ASSERT_EQ_INT(envelope_top_keys("{\"a\":{\"b\":1},\"c\":\"}\\\"{\",\"d\":[{\"e\":2}]}", keys),
                  3);
    ASSERT_STREQ(keys[0], "a");
    ASSERT_STREQ(keys[1], "c");
    ASSERT_STREQ(keys[2], "d");
    ASSERT_EQ_INT(envelope_top_keys("{\"a\":1", keys), -1);
    ASSERT_EQ_INT(envelope_top_keys("{\"a_key_far_too_long_for_this\":1}", keys), -1);
    ASSERT_EQ_INT(envelope_top_keys("{\"a\":1,\"b\":1,\"c\":1,\"d\":1,\"e\":1,\"f\":1,\"g\":1,"
                                    "\"h\":1,\"i\":1}",
                                    keys),
                  -1);
}

/* The one reply-type table (reply_noun_verb): every command names its type,
   on success and on failure (the last three fail), and every reply has the
   envelope shape. */
TEST(envelope_every_command_names_its_reply_type)
{
    static const TypeStep steps[] = {
        {{"--json", "add", "--key", "a", "first body"}, "memory.add"},
        {{"--json", "add", "--key", "b", "second body"}, "memory.add"},
        {{"--json", "search", "body"}, "memory.search"},
        {{"--json", "list"}, "memory.list"},
        {{"--json", "list", "--limit", "1"}, "memory.list"},
        {{"--json", "list", "--trash"}, "memory.list"},
        {{"--json", "get", "1"}, "memory.get"},
        {{"--json", "update", "1", "--text", "new body"}, "memory.update"},
        {{"--json", "tags"}, "tag.list"},
        {{"--json", "link", "1", "2"}, "memory.link"},
        {{"--json", "related", "1"}, "memory.related"},
        {{"--json", "unlink", "1", "2"}, "memory.unlink"},
        {{"--json", "rekey", "--key", "a", "--to-key", "c"}, "memory.rekey"},
        {{"--json", "delete", "2"}, "memory.delete"},
        {{"--json", "purge", "--deleted"}, "memory.purge"},
        {{"--json", "conflicts"}, "conflict.list"},
        {{"--json", "device"}, "device.get"},
        {{"--json", "device", "new"}, "device.new"},
        {{"--json", "brain", "new-id"}, "brain.new_id"},
        {{"--json", "repair", "search-index"}, "repair.search_index"},
        {{"--json", "conflict", "accept", "--id", "9", "--keep", "local"}, "conflict.accept"},
        {{"--json", "import", "--from-db", "/nonexistent/remember-type.db"}, "memory.import"},
        {{"--json", "bogus"}, "unknown"},
    };
    char *db = make_temp_db_path();
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    for (i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        size_t n = 0;
        char want[TYPE_PREFIX_CAP];
        CmdResult r;

        while (n < (size_t)TYPE_STEP_ARGS && steps[i].args[n] != NULL) {
            n++;
        }
        r = run_remember(db, steps[i].args, n, NULL);
        ASSERT_TRUE((size_t)snprintf(want, sizeof want,
                                     "{\"format\":\"remember-message\",\"version\":1,"
                                     "\"type\":\"%s.result\",",
                                     steps[i].type) < sizeof want);
        ASSERT_STR_PREFIX(r.out, want);
        assert_envelope_shape(r.out);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    free(db);
}

enum { DROPBOX_PATH_CAP = 4096 };

/* A brain under a Dropbox-named folder, so every open warns synced_folder.
   NULL when the folder cannot be made; the caller frees the path and removes dir. */
static char *given_synced_brain(char *dir_tmpl)
{
    char *path = NULL;

    if (mkdtemp(dir_tmpl) == NULL) {
        return NULL;
    }
    path = (char *)malloc(DROPBOX_PATH_CAP);
    if (path != NULL) {
        (void)snprintf(path, DROPBOX_PATH_CAP, "%s/b.db", dir_tmpl);
    }
    return path;
}

TEST(envelope_failure_keeps_its_type_and_carries_the_warnings_after_the_error)
{
    char dir[] = "/tmp/Dropbox-remember-env-XXXXXX";
    char *db = given_synced_brain(dir);
    const char *list[] = {"--json", "list", "--limit", "0"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, list, sizeof list / sizeof list[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(
        r.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.list.result\","
               "\"error\":{\"code\":\"usage\",\"message\":\"invalid --limit (must be 1..1000)\","
               "\"fix\":\"Run 'remember list --help' to see how to use it.\",\"retryable\":false},"
               "\"warnings\":[{\"code\":\"synced_folder\",\"message\":\"database path looks "
               "like a synced folder (iCloud/Dropbox/Google Drive); corruption risk - prefer "
               "a local disk\"}]}\n");
    cmd_result_free(&r);
    free(db);
    harness_remove_tree(dir);
}

TEST(envelope_warnings_keep_their_order_with_a_comma_between)
{
    char dir[] = "/tmp/Dropbox-remember-env-XXXXXX";
    char *db = given_synced_brain(dir);
    const char *list[] = {"--json", "list", "--trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, list, sizeof list / sizeof list[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"entries\":[]},\"next_cursor\":null,\"has_more\":false,"
                               "\"warnings\":[{\"code\":\"synced_folder\",");
    ASSERT_STR_CONTAINS(r.out, "local disk\"},{\"code\":\"flag_meaning_changed\",\"message\":"
                               "\"--trash now means the Trash (deleted memories). For expired "
                               "memories, use --expired.\",\"detail\":{\"flag\":\"--trash\","
                               "\"use\":\"--expired\"}}]}\n");
    cmd_result_free(&r);
    free(db);
    harness_remove_tree(dir);
}

/* --trash is the Deleted bin. --expired is expired memories. They are not aliases. */
TEST(envelope_trash_is_the_deleted_bin)
{
    char *db = make_temp_db_path();
    const char *live[] = {"add", "--key", "livek", "live body"};
    const char *exp[] = {"add", "--key", "expk", "--expires", "2020-01-01T00:00:00Z", "old body"};
    const char *del[] = {"add", "--key", "delk", "gone body"};
    const char *drop[] = {"delete", "--key", "delk"};
    const char *trash[] = {"list", "--trash"};
    const char *expired[] = {"list", "--expired"};
    const char *get_del[] = {"get", "--key", "delk"};
    const char *get_exp[] = {"get", "--key", "expk"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, live, sizeof live / sizeof live[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, exp, sizeof exp / sizeof exp[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, del, sizeof del / sizeof del[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, drop, sizeof drop / sizeof drop[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, trash, sizeof trash / sizeof trash[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "remember: warning: --trash now means the Trash (deleted memories). "
                        "For expired memories, use --expired.\n");
    ASSERT_STR_CONTAINS(r.out, "delk");
    ASSERT_STR_NOT_CONTAINS(r.out, "expk");
    ASSERT_STR_NOT_CONTAINS(r.out, "livek");
    cmd_result_free(&r);

    r = run_remember(db, expired, sizeof expired / sizeof expired[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "expk");
    ASSERT_STR_NOT_CONTAINS(r.out, "delk");
    cmd_result_free(&r);

    r = run_remember(db, get_del, sizeof get_del / sizeof get_del[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: \"delk\" is in the Trash. Add --trash to use it.\n");
    cmd_result_free(&r);

    r = run_remember(db, get_exp, sizeof get_exp / sizeof get_exp[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: \"expk\" has expired. Add --expired to use it.\n");
    cmd_result_free(&r);
    free(db);
}

static const char k_trash_warn[] =
    "remember: warning: --trash now means the Trash (deleted memories). "
    "For expired memories, use --expired.\n";

/* --strict and REMEMBER_STRICT=1 turn any warning into exit 4. Exit 4 has no other meaning. */
// NOLINTBEGIN(concurrency-mt-unsafe)
TEST(envelope_strict_makes_a_trash_warning_exit_4)
{
    char *db = make_temp_db_path();
    const char *trash[] = {"--strict", "list", "--trash"};
    const char *json[] = {"--json", "--strict", "list", "--trash"};
    const char *plain[] = {"list", "--trash"};
    const char *miss[] = {"--strict", "get", "--key", "missing"};
    const char *bad[] = {"--strict", "delete", "--trash", "--key", "nope"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    (void)unsetenv("REMEMBER_STRICT");

    r = run_remember(db, trash, sizeof trash / sizeof trash[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 4);
    ASSERT_STREQ(r.err, k_trash_warn);
    cmd_result_free(&r);

    r = run_remember(db, json, sizeof json / sizeof json[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 4);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"flag_meaning_changed\"");
    ASSERT_STR_CONTAINS(r.out, "\"detail\":{\"flag\":\"--trash\",\"use\":\"--expired\"}");
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_STRICT", "1", 1), 0);
    r = run_remember(db, plain, sizeof plain / sizeof plain[0], NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_STRICT"), 0);
    ASSERT_EQ_INT(r.exit_code, 4);
    ASSERT_STREQ(r.err, k_trash_warn);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_STRICT", "0", 1), 0);
    r = run_remember(db, plain, sizeof plain / sizeof plain[0], NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_STRICT"), 0);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, k_trash_warn);
    cmd_result_free(&r);

    r = run_remember(db, miss, sizeof miss / sizeof miss[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    ASSERT_STREQ(r.err, "remember: not found\n");
    cmd_result_free(&r);

    r = run_remember(db, bad, sizeof bad / sizeof bad[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 4);
    ASSERT_STREQ(r.err, "remember: warning: --trash now means the Trash (deleted memories). "
                        "For expired memories, use --expired.\n"
                        "remember: unknown option '--trash'\n");
    cmd_result_free(&r);
    free(db);
}
// NOLINTEND(concurrency-mt-unsafe)

TEST(envelope_help_states_trash_and_expired)
{
    char *db = make_temp_db_path();
    static const char *topics[] = {"get", "update", "list", "search", "tags", "rekey"};
    static const char trash_line[] =
        "  --trash     Use the Trash: memories you deleted. Restore them, or purge "
        "them for good.\n";
    static const char expired_line[] =
        "  --expired   Use expired memories: ones past their expiry time. They are "
        "not in the Trash.\n";
    size_t i = 0;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    for (i = 0; i < sizeof topics / sizeof topics[0]; i++) {
        const char *args[] = {"help", topics[i]};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, trash_line);
        ASSERT_STR_CONTAINS(r.out, expired_line);
        cmd_result_free(&r);
    }
    {
        const char *del[] = {"help", "delete"};

        r = run_remember(db, del, sizeof del / sizeof del[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "delete --expired hard-wipes one row.\n");
        ASSERT_STR_NOT_CONTAINS(r.out, "Use the Trash:");
        cmd_result_free(&r);
    }
    {
        const char *gone[] = {"purge-trash"};

        r = run_remember(db, gone, sizeof gone / sizeof gone[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, "remember: unknown command 'purge-trash'\n");
        cmd_result_free(&r);
    }
    free(db);
}

TEST(envelope_update_trash_undelete_restores)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "--key", "slot", "kept body"};
    const char *drop[] = {"delete", "--key", "slot"};
    const char *back[] = {"update", "--trash", "--key", "slot", "--undelete"};
    const char *get[] = {"get", "--key", "slot"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, drop, sizeof drop / sizeof drop[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, back, sizeof back / sizeof back[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, k_trash_warn);
    cmd_result_free(&r);
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "kept body\n");
    cmd_result_free(&r);
    free(db);
}

TEST(envelope_keyless_wrong_list_uses_the_uuid)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "solo"};
    const char *drop[] = {"delete", "1"};
    const char *get[] = {"get", "1"};
    const char *json[] = {"--json", "get", "1"};
    enum { WRONG_LIST_LINE = 160, JSON_FRAG = 120 };
    char *id = NULL;
    char want[WRONG_LIST_LINE];
    char frag[JSON_FRAG];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, drop, sizeof drop / sizeof drop[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    id = harness_sqlite_query_line(db, "SELECT id FROM entries WHERE local_id=1;");
    ASSERT_TRUE(id != NULL);
    if (id == NULL) {
        free(db);
        return;
    }
    (void)snprintf(want, sizeof want, "remember: \"%s\" is in the Trash. Add --trash to use it.\n",
                   id);
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, want);
    ASSERT_STR_NOT_CONTAINS(r.err, "\"1\"");
    cmd_result_free(&r);

    (void)snprintf(frag, sizeof frag, "\"message\":\"\\\"%s\\\" is in the Trash.\"", id);
    r = run_remember(db, json, sizeof json / sizeof json[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, frag);
    ASSERT_STR_CONTAINS(r.out, "\"fix\":\"Add --trash to use it.\"");
    cmd_result_free(&r);
    free(id);
    free(db);
}

/* A row with no key and an empty id has nothing to quote. The line stays the token. */
TEST(envelope_blank_id_wrong_list_keeps_the_deleted_token)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "solo"};
    const char *drop[] = {"delete", "1"};
    const char *get[] = {"get", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, drop, sizeof drop / sizeof drop[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(harness_sqlite_query_line(db, "UPDATE entries SET id='' WHERE local_id=1;"));
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: deleted\n");
    cmd_result_free(&r);
    free(db);
}

TEST(envelope_trash_and_expired_cannot_combine)
{
    char *db = make_temp_db_path();
    const char *mix[] = {"list", "--trash", "--expired"};
    const char *del[] = {"delete", "--trash", "--expired", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, mix, sizeof mix / sizeof mix[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: warning: --trash now means the Trash (deleted memories). "
                        "For expired memories, use --expired.\n"
                        "remember: cannot combine --expired and --trash\n");
    cmd_result_free(&r);
    r = run_remember(db, del, sizeof del / sizeof del[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot combine --expired and --trash");
    ASSERT_STR_NOT_CONTAINS(r.err, "unknown option");
    cmd_result_free(&r);
    free(db);
}

/* purge --trash selects the Deleted bin and warns. It is not an empty flag. */
TEST(envelope_purge_trash_selects_the_deleted_bin)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "gone"};
    const char *drop[] = {"delete", "1"};
    const char *purge[] = {"purge", "--trash"};
    const char *get[] = {"get", "--deleted", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, drop, sizeof drop / sizeof drop[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, purge, sizeof purge / sizeof purge[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, k_trash_warn);
    ASSERT_STREQ(r.out, "1\n");
    cmd_result_free(&r);
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    free(db);
}

/* The shim test needs the CLI built with REMEMBER_TEST_VFS (the gate configures it ON).
   setenv/unsetenv drive the shim; the test runner is single-threaded. */
#ifdef REMEMBER_TEST_VFS
// NOLINTBEGIN(concurrency-mt-unsafe)
/* Reads 0..17 succeed; the 19th, the link read after the commit, fails (pinned SQLite;
   N 17..20 all land there). decision:plan21-add-link-read-fails-loud. */
TEST(envelope_add_link_read_error_after_the_write_exits_1_and_keeps_the_write)
{
    char *db = make_temp_db_path();
    const char *add[] = {"--json", "add", "--key", "hub", "new hub body"};
    const char *get[] = {"get", "--key", "hub"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "hub");
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_READ", "18", 1), 0);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_READ"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,"
                             "\"type\":\"memory.add.result\",\"error\":{\"code\":\"io_error\"");
    ASSERT_STR_CONTAINS(r.out, "SQLITE_IOERR_READ");
    cmd_result_free(&r);
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_STREQ(r.out, "new hub body\n");
    cmd_result_free(&r);
    free(db);
}
// NOLINTEND(concurrency-mt-unsafe)
#endif /* REMEMBER_TEST_VFS */

enum {
    UUID_CANON_LEN = 36,
    UUID_DASH_0 = 8,
    UUID_DASH_1 = 13,
    UUID_DASH_2 = 18,
    UUID_DASH_3 = 23,
    UUID_VER_CHAR = 14
};

/* 1 when s starts with a canonical lowercase UUID v7. */
static int uuid_v7_at(const char *s)
{
    size_t i = 0;

    if (s == NULL || strlen(s) < UUID_CANON_LEN) {
        return 0;
    }
    for (i = 0; i < UUID_CANON_LEN; i++) {
        char c = s[i];

        if (i == (size_t)UUID_DASH_0 || i == (size_t)UUID_DASH_1 || i == (size_t)UUID_DASH_2 ||
            i == (size_t)UUID_DASH_3) {
            if (c != '-') {
                return 0;
            }
            continue;
        }
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
            return 0;
        }
    }
    return s[UUID_VER_CHAR] == '7';
}

enum { MEMORY_ID_LEN = 36 };

/* Copy data.memory.id. The bytes are the UUID the next get passes back. */
static int copy_memory_id(const char *json, char out[MEMORY_ID_LEN + 1])
{
    const char *key = "\"memory\":{\"id\":\"";
    const char *p = NULL;
    size_t i = 0;

    if (json == NULL) {
        return -1;
    }
    p = strstr(json, key);
    if (p == NULL) {
        return -1;
    }
    p += strlen(key);
    for (i = 0; i < (size_t)MEMORY_ID_LEN; i++) {
        if (p[i] == '\0' || p[i] == '"') {
            return -1;
        }
        out[i] = p[i];
    }
    if (p[MEMORY_ID_LEN] != '"') {
        return -1;
    }
    out[MEMORY_ID_LEN] = '\0';
    return 0;
}

/* 008-13 and the C-01 positional sentence (JE-3.4). get 1 and get <uuid> are
   the same memory. Anything else, including mem_…, is invalid_argument. */
TEST(envelope_get_by_digits_and_by_uuid_is_the_same_memory)
{
    static const char k_plain[] = "remember: \"nope\" isn't a memory id. Use a number from "
                                  "'remember list', a UUID, or --key KEY.\n";
    static const char k_json[] =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\","
        "\"error\":{\"code\":\"invalid_argument\",\"message\":\"\\\"nope\\\" isn't a memory id.\","
        "\"fix\":\"Use a number from 'remember list', a UUID, or --key KEY.\","
        "\"retryable\":false}}\n";
    static const char k_mem_plain[] =
        "remember: \"mem_01a14513-6501-7928-b9c4-2cc649cb59a9\" isn't a memory id. Use a number "
        "from 'remember list', a UUID, or --key KEY.\n";
    char *db = make_temp_db_path();
    char id[MEMORY_ID_LEN + 1];
    const char *by_digits[] = {"--json", "get", "1"};
    CmdResult digits;
    CmdResult by_uuid;
    CmdResult bad;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    digits = run_remember(db, by_digits, sizeof by_digits / sizeof by_digits[0], NULL);
    ASSERT_EQ_INT(digits.exit_code, 0);
    ASSERT_EQ_INT(copy_memory_id(digits.out, id), 0);
    ASSERT_STR_CONTAINS(digits.out, "\"local_id\":1,\"key\":\"k\"");

    {
        const char *args[] = {"--json", "get", id};

        by_uuid = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(by_uuid.exit_code, 0);
    ASSERT_STR_CONTAINS(by_uuid.out, "\"local_id\":1,\"key\":\"k\"");
    ASSERT_STR_CONTAINS(by_uuid.out, id);
    cmd_result_free(&digits);
    cmd_result_free(&by_uuid);

    {
        const char *args[] = {"get", "nope"};

        bad = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(bad.exit_code, 1);
    ASSERT_STREQ(bad.err, k_plain);
    ASSERT_STREQ(bad.out, "");
    cmd_result_free(&bad);

    {
        const char *args[] = {"--json", "get", "nope"};

        bad = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(bad.exit_code, 1);
    ASSERT_STREQ(bad.out, k_json);
    ASSERT_STREQ(bad.err, "");
    cmd_result_free(&bad);

    {
        const char *args[] = {"get", "mem_01a14513-6501-7928-b9c4-2cc649cb59a9"};

        bad = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(bad.exit_code, 1);
    ASSERT_STREQ(bad.err, k_mem_plain);
    cmd_result_free(&bad);
    free(db);
}

static const char k_locator_fix[] = "Use a number from 'remember list', a UUID, or --key KEY.";

/* The C-01 line for a positional whose bytes are shown unchanged. */
static char *plain_locator_line(const char *value)
{
    int wrote = snprintf(NULL, 0, "remember: \"%s\" isn't a memory id. %s\n", value, k_locator_fix);
    char *buf = NULL;

    if (wrote < 0) {
        return NULL;
    }
    buf = (char *)malloc((size_t)wrote + 1U);
    if (buf == NULL) {
        return NULL;
    }
    if (snprintf(buf, (size_t)wrote + 1U, "remember: \"%s\" isn't a memory id. %s\n", value,
                 k_locator_fix) != wrote) {
        free(buf);
        return NULL;
    }
    return buf;
}

static char *json_locator_line(const char *value)
{
    int wrote = snprintf(NULL, 0,
                         "{\"format\":\"remember-message\",\"version\":1,"
                         "\"type\":\"memory.get.result\",\"error\":{\"code\":\"invalid_argument\","
                         "\"message\":\"\\\"%s\\\" isn't a memory id.\",\"fix\":\"%s\","
                         "\"retryable\":false}}\n",
                         value, k_locator_fix);
    char *buf = NULL;

    if (wrote < 0) {
        return NULL;
    }
    buf = (char *)malloc((size_t)wrote + 1U);
    if (buf == NULL) {
        return NULL;
    }
    if (snprintf(buf, (size_t)wrote + 1U,
                 "{\"format\":\"remember-message\",\"version\":1,"
                 "\"type\":\"memory.get.result\",\"error\":{\"code\":\"invalid_argument\","
                 "\"message\":\"\\\"%s\\\" isn't a memory id.\",\"fix\":\"%s\","
                 "\"retryable\":false}}\n",
                 value, k_locator_fix) != wrote) {
        free(buf);
        return NULL;
    }
    return buf;
}

enum { JSON_ARGV_CAP = 6, LONG_POSITIONAL = 300 };

/* Plain and JSON for one positional. value must need no human mapping or JSON escape. */
static void assert_get_rejects_positional(const char *db, const char *const *human, size_t n,
                                          const char *value)
{
    char *plain = plain_locator_line(value);
    char *json = json_locator_line(value);
    const char *json_argv[JSON_ARGV_CAP];
    CmdResult r;
    size_t i = 0;

    ASSERT_TRUE(plain != NULL);
    ASSERT_TRUE(json != NULL);
    if (plain == NULL || json == NULL) {
        free(plain);
        free(json);
        return;
    }
    r = run_remember(db, human, n, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, plain);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);

    json_argv[0] = "--json";
    for (i = 0; i < n && i + 1U < sizeof json_argv / sizeof json_argv[0]; i++) {
        json_argv[i + 1U] = human[i];
    }
    r = run_remember(db, json_argv, n + 1U, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, json);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(plain);
    free(json);
}

/* The approved plain line, as a literal, for one command's positional. */
static void assert_nope_is_not_a_memory_id(const char *db, const char *const *args, size_t n)
{
    static const char k_plain[] = "remember: \"nope\" isn't a memory id. Use a number from "
                                  "'remember list', a UUID, or --key KEY.\n";
    CmdResult r = run_remember(db, args, n, NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, k_plain);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
}

/* 008-13 on delete, update, rekey, and both graph ends. Digits 01 are local_id 1. */
TEST(envelope_uuid_positional_updates_deletes_and_links)
{
    char *db = make_temp_db_path();
    char id_a[MEMORY_ID_LEN + 1];
    char id_b[MEMORY_ID_LEN + 1];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    {
        const char *add[] = {"--json", "add", "other"};

        r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_EQ_INT(copy_memory_id(r.out, id_b), 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "01"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_EQ_INT(copy_memory_id(r.out, id_a), 0);
        ASSERT_STR_CONTAINS(r.out, "\"local_id\":1,\"key\":\"k\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"update", id_a, "--text", "changed"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"body\":\"changed\"");
        ASSERT_STR_CONTAINS(r.out, id_a);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"rekey", id_a, "--to-key", "nk"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "--key", "nk"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, id_a);
        ASSERT_STR_CONTAINS(r.out, "\"key\":\"nk\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "link", id_a, id_b, "--kind", "cites"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
        ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "related", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, id_b);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"unlink", id_a, id_b};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"delete", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 3);
        ASSERT_STR_CONTAINS(r.out, "\"code\":\"deleted\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "link", "--deleted", "--from", id_b, "--to", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
        cmd_result_free(&r);
    }
    free(db);
}

/* A canonical UUID that is not in the brain is not_found. Anything that is not
   a UUID or digits, including a sign, a space, and mem_, is invalid_argument. */
TEST(envelope_positional_rejects_what_is_not_a_memory_id)
{
    static const char k_missing[] = "018f6b2e-4c3a-7b2e-8f1a-2b3c4d5e6f70";
    static const char k_upper[] = "018F6B2E-4C3A-7B2E-8F1A-2B3C4D5E6F70";
    static const char k_nl_plain[] = "remember: \"nl?\" isn't a memory id. Use a number from "
                                     "'remember list', a UUID, or --key KEY.\n";
    static const char k_nl_json[] =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\","
        "\"error\":{\"code\":\"invalid_argument\",\"message\":\"\\\"nl\\n\\\" isn't a memory "
        "id.\",\"fix\":\"Use a number from 'remember list', a UUID, or --key KEY.\","
        "\"retryable\":false}}\n";
    static const char k_esc_plain[] = "remember: \"esc?\" isn't a memory id. Use a number from "
                                      "'remember list', a UUID, or --key KEY.\n";
    static const char k_esc_json[] =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\","
        "\"error\":{\"code\":\"invalid_argument\",\"message\":\"\\\"esc\\u001b\\\" isn't a "
        "memory id.\",\"fix\":\"Use a number from 'remember list', a UUID, or --key KEY.\","
        "\"retryable\":false}}\n";
    char *db = make_temp_db_path();
    char braces[MEMORY_ID_LEN + 3];
    char long_id[LONG_POSITIONAL + 1];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    memset(long_id, 'a', sizeof long_id - 1U);
    long_id[sizeof long_id - 1U] = '\0';
    ASSERT_TRUE(snprintf(braces, sizeof braces, "{%s}", k_missing) > 0);

    {
        const char *human[] = {"get", "0"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "0");
    }
    {
        const char *human[] = {"get", "+1"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "+1");
    }
    {
        const char *human[] = {"get", " 1"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], " 1");
    }
    {
        const char *human[] = {"get", ""};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "");
    }
    {
        const char *human[] = {"get", "--", "-1"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "-1");
    }
    {
        const char *human[] = {"get", k_upper};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], k_upper);
    }
    {
        const char *human[] = {"get", braces};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], braces);
    }
    {
        const char *human[] = {"get", long_id};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], long_id);
    }
    {
        char newline_id[] = {'n', 'l', '\n', '\0'};
        const char *human[] = {"get", newline_id};
        const char *json_get[] = {"--json", "get", newline_id};

        r = run_remember(db, human, sizeof human / sizeof human[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, k_nl_plain);
        ASSERT_STREQ(r.out, "");
        cmd_result_free(&r);
        r = run_remember(db, json_get, sizeof json_get / sizeof json_get[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.out, k_nl_json);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    {
        char escape_id[] = {'e', 's', 'c', '\x1b', '\0'};
        const char *human[] = {"get", escape_id};
        const char *json_get[] = {"--json", "get", escape_id};

        r = run_remember(db, human, sizeof human / sizeof human[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, k_esc_plain);
        ASSERT_STREQ(r.out, "");
        cmd_result_free(&r);
        r = run_remember(db, json_get, sizeof json_get / sizeof json_get[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.out, k_esc_json);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", k_missing};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.out, "\"code\":\"not_found\"");
        ASSERT_STR_NOT_CONTAINS(r.out, "invalid_argument");
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"delete", k_missing};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        ASSERT_STR_NOT_CONTAINS(r.err, "isn't a memory id");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"link", k_missing, "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"link", "--from-key", "missing-key", "--to", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"link", "--deleted", "--from-key", "missing-key", "--to", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"update", k_missing, "--text", "x"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"get", "--offset", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--offset'");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"get", "--json-errors"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--json-errors'");
        cmd_result_free(&r);
    }
    {
        const char *delete_args[] = {"delete", "nope"};
        const char *update_args[] = {"update", "nope", "--text", "x"};
        const char *rekey_args[] = {"rekey", "nope", "--to-key", "z"};
        const char *link_args[] = {"link", "nope", "1"};
        const char *unlink_args[] = {"unlink", "nope", "1"};
        const char *related_args[] = {"related", "nope"};

        assert_nope_is_not_a_memory_id(db, delete_args, sizeof delete_args / sizeof delete_args[0]);
        assert_nope_is_not_a_memory_id(db, update_args, sizeof update_args / sizeof update_args[0]);
        assert_nope_is_not_a_memory_id(db, rekey_args, sizeof rekey_args / sizeof rekey_args[0]);
        assert_nope_is_not_a_memory_id(db, link_args, sizeof link_args / sizeof link_args[0]);
        assert_nope_is_not_a_memory_id(db, unlink_args, sizeof unlink_args / sizeof unlink_args[0]);
        assert_nope_is_not_a_memory_id(db, related_args,
                                       sizeof related_args / sizeof related_args[0]);
    }
    free(db);
}

/* Unflagged delete of an expired memory, addressed by its UUID, soft-deletes.
   get of that UUID without a bin flag is the expired bin, not invalid_argument. */
TEST(envelope_delete_expired_by_uuid_is_a_soft_delete)
{
    char *db = make_temp_db_path();
    char id[MEMORY_ID_LEN + 1];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    {
        const char *add[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old"};

        r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "--expired", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_EQ_INT(copy_memory_id(r.out, id), 0);
        ASSERT_STR_CONTAINS(r.out, "\"bin\":\"expired\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", id};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 3);
        ASSERT_STR_CONTAINS(r.out, "\"code\":\"expired\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"delete", id};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "--deleted", id};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
        cmd_result_free(&r);
    }
    free(db);
}

/* JSON id is the old sync_id. The integer is local_id. The three flags are gone.
   Stage 3 owns the C-01 locator sentence. */
TEST(envelope_memory_id_is_the_uuid_and_the_old_flags_are_unknown)
{
    static const char *const flags[] = {"--sync-id", "--local-id", "--id"};
    char *db = make_temp_db_path();
    const char *get[] = {"--json", "get", "1"};
    const char *memory = NULL;
    CmdResult r;
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    memory = strstr(r.out, "\"memory\":{\"id\":\"");
    ASSERT_TRUE(memory != NULL);
    if (memory != NULL) {
        ASSERT_EQ_INT(uuid_v7_at(memory + strlen("\"memory\":{\"id\":\"")), 1);
    }
    ASSERT_STR_CONTAINS(r.out, "\",\"local_id\":1,\"key\":\"k\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "sync_id");
    cmd_result_free(&r);

    for (i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        const char *args[] = {"get", flags[i], "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '");
        ASSERT_STR_CONTAINS(r.err, flags[i]);
        cmd_result_free(&r);
    }
    {
        const char *from_sync[] = {"link", "--from-sync-id", "1", "--to", "2"};
        const char *to_sync[] = {"link", "--from", "1", "--to-sync-id", "2"};

        r = run_remember(db, from_sync, sizeof from_sync / sizeof from_sync[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--from-sync-id'");
        cmd_result_free(&r);
        r = run_remember(db, to_sync, sizeof to_sync / sizeof to_sync[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--to-sync-id'");
        cmd_result_free(&r);
    }
    free(db);
}

enum { FIELD_KEY_CAP = 64, BRAIN_REPLY_CAP = 320 };

/* Copy the UUID at `at` when it is canonical. -1 leaves `out` untouched. */
static int take_uuid(const char *at, char out[UUID_CANON_LEN + 1])
{
    size_t i = 0;

    if (at == NULL) {
        return -1;
    }
    for (i = 0; i < (size_t)UUID_CANON_LEN; i++) {
        if (at[i] == '\0') {
            return -1;
        }
        out[i] = at[i];
    }
    out[UUID_CANON_LEN] = '\0';
    if (!uuid_v7_is_canonical(out)) {
        return -1;
    }
    return 0;
}

/* The UUID after `"<field>":"` in one JSON object. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) json, then the field name
static int take_field_uuid(const char *json, const char *field, char out[UUID_CANON_LEN + 1])
{
    char key[FIELD_KEY_CAP];
    const char *found = NULL;
    int wrote = 0;

    wrote = snprintf(key, sizeof(key), "\"%s\":\"", field);
    if (wrote < 0 || (size_t)wrote >= sizeof(key) || json == NULL) {
        return -1;
    }
    found = strstr(json, key);
    if (found == NULL) {
        return -1;
    }
    return take_uuid(found + (size_t)wrote, out);
}

/* Plain stdout is the new id and one newline. A short line is not indexed. */
static void assert_plain_new_id(const char *out, char id[UUID_CANON_LEN + 1])
{
    ASSERT_TRUE(out != NULL && strlen(out) == (size_t)UUID_CANON_LEN + 1U);
    if (out == NULL || strlen(out) != (size_t)UUID_CANON_LEN + 1U) {
        return;
    }
    ASSERT_EQ_INT(take_uuid(out, id), 0);
    ASSERT_EQ_INT(out[UUID_CANON_LEN], '\n');
}

/* JE-3.9 / 008-14 write half / probe 7. JE-3.10 stays out: a copied file keeps brain_id. */
TEST(envelope_brain_new_id_replaces_only_the_brain_id)
{
    static const char k_memory[] = "SELECT body || '|' || version_vector || '|' || change_seq || "
                                   "'|' || updated_at || '|' || created_at || '|' || created_by "
                                   "|| '|' || updated_by FROM entries WHERE local_id = 1;";
    static const char k_meta[] = "SELECT change_seq || '|' || min_reader_version FROM brain_meta;";
    static const char k_counts[] = "SELECT (SELECT COUNT(*) FROM entries) || '|' || (SELECT "
                                   "COUNT(*) FROM entry_links) || '|' || (SELECT COUNT(*) FROM "
                                   "tombstones);";
    static const char k_newer[] =
        "remember: this brain was made by a newer version of remember, so it is read-only here. "
        "You can still read and export it. This brain uses schema 6; this remember reads up to "
        "schema 6. Upgrade remember to make changes.\n";
    char *db = make_temp_db_path();
    char *bare = make_temp_db_path();
    char *born = make_temp_db_path();
    const char *add[] = {"add", "--key", "k", "a body"};
    const char *plain[] = {"brain", "new-id"};
    const char *again[] = {"--json", "brain", "new-id"};
    const char *extra[] = {"--json", "brain", "new-id", "extra"};
    const char *alone[] = {"brain"};
    const char *brains[] = {"brains"};
    const char *help[] = {"help", "brain"};
    const char *open_copy[] = {"--no-create", "get", "1"};
    char first_id[UUID_CANON_LEN + 1] = {0};
    char second_id[UUID_CANON_LEN + 1] = {0};
    char previous_id[UUID_CANON_LEN + 1] = {0};
    char want[BRAIN_REPLY_CAP];
    char *original_id = NULL;
    char *memory_before = NULL;
    char *meta_before = NULL;
    char *counts_before = NULL;
    char *copy_path = NULL;
    char *copy_id = NULL;
    char *source_id = NULL;
    CmdResult r;

    ASSERT_TRUE(db != NULL && bare != NULL && born != NULL);
    if (db == NULL || bare == NULL || born == NULL) {
        free(db);
        free(bare);
        free(born);
        return;
    }

    r = run_remember(bare, alone, sizeof alone / sizeof alone[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: brain takes 'new-id'\n");
    ASSERT_TRUE(access(bare, F_OK) != 0);
    cmd_result_free(&r);

    r = run_remember(born, plain, sizeof plain / sizeof plain[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    assert_plain_new_id(r.out, first_id);
    cmd_result_free(&r);
    original_id = harness_sqlite_query_line(born, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(original_id, first_id);
    free(original_id);

    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    original_id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");
    memory_before = harness_sqlite_query_line(db, k_memory);
    meta_before = harness_sqlite_query_line(db, k_meta);
    counts_before = harness_sqlite_query_line(db, k_counts);
    ASSERT_TRUE(original_id != NULL && memory_before != NULL && meta_before != NULL &&
                counts_before != NULL);

    r = run_remember(db, plain, sizeof plain / sizeof plain[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    assert_plain_new_id(r.out, first_id);
    ASSERT_TRUE(strcmp(first_id, original_id != NULL ? original_id : "") != 0);
    cmd_result_free(&r);
    source_id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(source_id, first_id);
    free(source_id);
    source_id = harness_sqlite_query_line(db, k_memory);
    ASSERT_STREQ(source_id, memory_before);
    free(source_id);
    source_id = harness_sqlite_query_line(db, k_meta);
    ASSERT_STREQ(source_id, meta_before);
    free(source_id);
    source_id = harness_sqlite_query_line(db, k_counts);
    ASSERT_STREQ(source_id, counts_before);
    free(source_id);

    r = run_remember(db, again, sizeof again / sizeof again[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_EQ_INT(take_field_uuid(r.out, "brain_id", second_id), 0);
    ASSERT_EQ_INT(take_field_uuid(r.out, "previous_brain_id", previous_id), 0);
    ASSERT_STREQ(previous_id, first_id);
    ASSERT_TRUE(strcmp(second_id, first_id) != 0);
    ASSERT_TRUE((size_t)snprintf(want, sizeof(want),
                                 "{\"format\":\"remember-message\",\"version\":1,"
                                 "\"type\":\"brain.new_id.result\",\"data\":{\"brain_id\":\"%s\","
                                 "\"previous_brain_id\":\"%s\"}}\n",
                                 second_id, previous_id) < sizeof(want));
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    source_id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(source_id, second_id);
    free(source_id);
    source_id = harness_sqlite_query_line(db, k_memory);
    ASSERT_STREQ(source_id, memory_before);
    free(source_id);
    source_id = harness_sqlite_query_line(db, k_meta);
    ASSERT_STREQ(source_id, meta_before);
    free(source_id);

    copy_path = harness_side_path(db, ".copy");
    ASSERT_EQ_INT(harness_copy_file(db, copy_path), 0);
    r = run_remember(copy_path, open_copy, sizeof open_copy / sizeof open_copy[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "a body");
    cmd_result_free(&r);
    copy_id = harness_sqlite_query_line(copy_path, "SELECT brain_id FROM brain_meta;");
    source_id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(copy_id, second_id);
    ASSERT_STREQ(source_id, second_id);
    free(copy_id);
    free(source_id);
    copy_id = harness_sqlite_query_line(copy_path, k_memory);
    ASSERT_STREQ(copy_id, memory_before);
    free(copy_id);
    free(copy_path);

    r = run_remember(db, extra, sizeof extra / sizeof extra[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"brain.new_id."
                        "result\",\"error\":{\"code\":\"usage\",\"message\":\"brain new-id takes "
                        "no arguments\",\"fix\":\"Run 'remember brain --help' to see how to use "
                        "it.\",\"retryable\":false}}\n");
    cmd_result_free(&r);
    source_id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(source_id, second_id);
    free(source_id);

    r = run_remember(db, brains, sizeof brains / sizeof brains[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: unknown command 'brains'\n");
    cmd_result_free(&r);

    r = run_remember(db, help, sizeof help / sizeof help[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "remember brain new-id");
    ASSERT_STR_CONTAINS(r.out, "lowercase UUID v7");
    ASSERT_STR_CONTAINS(r.out, "does not change change_seq, a version vector, or a memory");
    ASSERT_STR_CONTAINS(r.out, "brain.new_id.result");
    ASSERT_STR_CONTAINS(r.out, "previous_brain_id");
    cmd_result_free(&r);

    free(harness_sqlite_query_line(db, "UPDATE brain_meta SET min_reader_version = '0.4.0';"));
    r = run_remember(db, plain, sizeof plain / sizeof plain[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 75);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_newer);
    cmd_result_free(&r);
    source_id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(source_id, second_id);
    free(source_id);
    source_id = harness_sqlite_query_line(db, k_memory);
    ASSERT_STREQ(source_id, memory_before);
    free(source_id);

    free(original_id);
    free(memory_before);
    free(meta_before);
    free(counts_before);
    free(db);
    free(bare);
    free(born);
}

/* C-02. General help prints the locked lines. The command footer lists the flags
   with the other globals, including --strict (left out until this stage). */
enum { UUID_TEXT = 36, ID_CAP = 64, KEY_CAP = 8, NOW_CAP = 32 };

/* Add one memory with REMEMBER_TEST_FIXED_IDS set to value; copy out its id. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void add_with_fixed_ids(const char *db, const char *value, const char *key, char *id,
                               size_t idsz)
{
    const char *add[] = {"--json", "add", "--key", key, "b"};
    const char *at = NULL;
    CmdResult r;

    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_FIXED_IDS", value, 1);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    (void)unsetenv("REMEMBER_TEST_FIXED_IDS");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_EQ_INT(r.exit_code, 0);
    at = strstr(r.out, "{\"id\":\"");
    ASSERT_TRUE(at != NULL && idsz > UUID_TEXT);
    if (at != NULL && idsz > UUID_TEXT) {
        (void)snprintf(id, idsz, "%.36s", at + strlen("{\"id\":\""));
    }
    cmd_result_free(&r);
}

/* 008-10 / decision:json-test-fixed-ids: the test CLI counts ids up from N and stops
   the clock, so a golden can hold whole replies. The device id is minted first. */
TEST(envelope_fixed_ids_make_replies_predictable)
{
    char *db = make_temp_db_path();
    const char *add[] = {"--json", "add", "--key", "k", "hello"};
    char id[ID_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_FIXED_IDS", "1", 1);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    (void)unsetenv("REMEMBER_TEST_FIXED_IDS");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\","
                 "\"data\":{\"action\":\"created\",\"memory\":{\"id\":\"00000000-0000-7000-8000-"
                 "000000000003\",\"local_id\":1,\"key\":\"k\",\"body\":\"hello\",\"tags\":[],"
                 "\"created_by\":{\"kind\":\"human\",\"id\":null},\"updated_by\":{\"kind\":"
                 "\"human\",\"id\":null},\"created_at\":\"2026-01-01T00:00:00.000Z\","
                 "\"updated_at\":\"2026-01-01T00:00:00.000Z\",\"expires_at\":null,"
                 "\"deleted_at\":null,\"bin\":\"live\",\"version_vector\":{\"00000000-0000-7000-"
                 "8000-000000000001\":1},\"links\":[]}}}\n");
    cmd_result_free(&r);
    /* A later command starts where the caller says, so ids stay unique. N is decimal. */
    add_with_fixed_ids(db, "100", "k2", id, sizeof id);
    ASSERT_STREQ(id, "00000000-0000-7000-8000-000000000064");
    add_with_fixed_ids(db, "281474976710655", "k3", id, sizeof id);
    ASSERT_STREQ(id, "00000000-0000-7000-8000-ffffffffffff");
    free(db);
}

/* Anything but a number in [1, 2^48) leaves ids and time real. */
TEST(envelope_fixed_ids_ignore_a_bad_value)
{
    const char *bad[] = {"0", "01", " 1", "x", "1x", "281474976710656", "99999999999999999999999"};
    char key[KEY_CAP];
    char id[ID_CAP];
    size_t i = 0;
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        (void)snprintf(key, sizeof key, "k%zu", i);
        add_with_fixed_ids(db, bad[i], key, id, sizeof id);
        ASSERT_TRUE(strncmp(id, "00000000-", 9) != 0);
    }
    free(db);
}

/* 008-10: a build without the test CLI define (release, libremember, this
   in-process copy) ignores the variable. */
TEST(envelope_fixed_ids_are_ignored_outside_the_test_cli)
{
    char now[NOW_CAP];

    // NOLINTBEGIN(concurrency-mt-unsafe) one thread; restored before the assert
    (void)setenv("REMEMBER_TEST_FIXED_IDS", "1", 1);
    ASSERT_EQ_INT(utc_now(now, sizeof now), 0);
    (void)unsetenv("REMEMBER_TEST_FIXED_IDS");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_TRUE(strcmp(now, "2026-01-01T00:00:00.000Z") != 0);
}

/* H-16.11: every write accepts --hooks and --no-hooks and prints the locked help
   lines. Until plan 25 they change nothing. A read rejects them. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST(envelope_every_write_accepts_the_hook_flags)
{
    static const char hooks_line[] = "  --hooks     Run matching hooks for this write, also ones "
                                     "you turned off for local writes.\n";
    static const char no_hooks_line[] = "  --no-hooks  Run no hooks for this write.\n";
    static const char *const write_topics[] = {"add",    "update", "delete", "rekey",   "link",
                                               "unlink", "purge",  "import", "conflict"};
    static const char *const read_topics[] = {"get", "list", "search", "related"};
    const char *add1[] = {"add", "--hooks", "one"};
    const char *add2[] = {"add", "--no-hooks", "two"};
    const char *update[] = {"update", "--no-hooks", "--text", "uno", "1"};
    const char *link[] = {"link", "--hooks", "1", "2"};
    const char *unlink[] = {"unlink", "--no-hooks", "1", "2"};
    const char *rekey[] = {"rekey", "--hooks", "--to-key", "k:one", "1"};
    const char *del[] = {"delete", "--no-hooks", "2"};
    const char *purge[] = {"purge", "--deleted", "--hooks"};
    const char *import[] = {"import", "--no-hooks"};
    const char *accept[] = {"conflict", "accept", "--hooks", "--id", "1", "--keep", "local"};
    const char *get[] = {"get", "--hooks", "1"};
    const char *list[] = {"list", "--no-hooks"};
    const char *search[] = {"search", "--hooks", "uno"};
    const char *related[] = {"related", "--no-hooks", "1"};
    const char *const *writes[] = {add1, add2, update, link, unlink, rekey, del, purge};
    const size_t writes_n[] = {3U, 3U, 5U, 4U, 4U, 5U, 3U, 3U};
    const char *const *reads[] = {get, list, search, related};
    const size_t reads_n[] = {3U, 2U, 3U, 3U};
    char *db = make_temp_db_path();
    CmdResult r;
    size_t i = 0U;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    for (i = 0U; i < sizeof(writes) / sizeof(writes[0]); i++) {
        r = run_remember(db, writes[i], writes_n[i], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    /* import and conflict accept fail later, on their own required options. */
    r = run_remember(db, import, sizeof(import) / sizeof(import[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: import requires exactly one of --from-json, "
                        "--from-md, --from-csv, --from-db\n");
    cmd_result_free(&r);
    r = run_remember(db, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_STR_NOT_CONTAINS(r.err, "unknown option");
    ASSERT_STR_NOT_CONTAINS(r.err, "takes no positional");
    cmd_result_free(&r);
    for (i = 0U; i < sizeof(reads) / sizeof(reads[0]); i++) {
        r = run_remember(db, reads[i], reads_n[i], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "remember: unknown option '--");
        ASSERT_STR_CONTAINS(r.err, "hooks'");
        cmd_result_free(&r);
    }
    for (i = 0U; i < sizeof(write_topics) / sizeof(write_topics[0]); i++) {
        const char *help[] = {"help", write_topics[i]};

        r = run_remember(db, help, 2U, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, hooks_line);
        ASSERT_STR_CONTAINS(r.out, no_hooks_line);
        cmd_result_free(&r);
    }
    for (i = 0U; i < sizeof(read_topics) / sizeof(read_topics[0]); i++) {
        const char *help[] = {"help", read_topics[i]};

        r = run_remember(db, help, 2U, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_NOT_CONTAINS(r.out, "--hooks");
        cmd_result_free(&r);
    }
    free(db);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(envelope_help_prints_the_color_and_quiet_lines)
{
    char *db = make_temp_db_path();
    const char *general[] = {"--help"};
    const char *topic[] = {"help", "get"};
    static const char color_line[] =
        "  --color     Use color: auto, always, or never. auto uses color only on a terminal, "
        "when NO_COLOR is unset and TERM isn't dumb.\n";
    static const char quiet_line[] = "  --quiet     Turn off progress lines on stderr.\n";
    static const char usage_globals[] =
        "[--db PATH] [--json] [--strict] [--color=auto|always|never] [--quiet] "
        "[--busy-timeout MS] [--no-create]";
    static const char footer[] = "Global options: --db PATH, --json, --strict, "
                                 "--color=auto|always|never, --quiet, "
                                 "--busy-timeout MS, --no-create, --help, --version\n";
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, general, sizeof general / sizeof general[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, color_line);
    ASSERT_STR_CONTAINS(r.out, quiet_line);
    ASSERT_STR_CONTAINS(r.out, usage_globals);
    cmd_result_free(&r);
    r = run_remember(db, topic, sizeof topic / sizeof topic[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, footer);
    cmd_result_free(&r);
    free(db);
}

/* Single-threaded tests. getenv, setenv, unsetenv, and ptsname have no C11 form. */
// NOLINTBEGIN(concurrency-mt-unsafe)

enum {
    PROGRESS_GAP_MS = 1000,   /* plan: at most one progress line per second */
    PROGRESS_WIDE = 300,      /* a line long enough to show it is not truncated */
    PROGRESS_DUE_GAPS = 5,    /* later than the one-second line, so another line is due */
    PROGRESS_REWIND_GAPS = 4, /* earlier than DUE, so the clock suppresses it */
    PROGRESS_EXACT_GAPS = 6,  /* exactly one second after DUE, so a line is due again */
    PROMPT_SLOT = 8,          /* "Ask: " and "yes\r\n" both fit */
    PROMPT_TINY = 3,          /* fgets keeps two characters, so "yes\n" does not fit */
    PTY_POLL_MS = 200,        /* a ready pty returns at once; this is only the failure wait */
    CAPTURE_ARGV = 8
};

typedef struct {
    char *saved;
    bool had;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7];
} SavedEnv;

typedef struct {
    const char *typed;
    const char *prompt;
    size_t cap;
} PromptAsk;

typedef struct {
    char *text;
    size_t text_cap;
    char *shown;
    size_t shown_cap;
    char *echo;
    size_t echo_cap;
    int *echo_on;
} PromptOut;

/* A stream whose fileno is -1 and whose writes fail. Covers the not-a-terminal
   side and a prompt that must stop before it reads. */
#ifdef __APPLE__
static int dead_write(void *cookie, const char *data, int length)
{
    (void)cookie;
    (void)data;
    (void)length;
    errno = EIO;
    return -1;
}

static FILE *open_dead_stream(void)
{
    FILE *stream = funopen(NULL, NULL, dead_write, NULL, NULL);

    if (stream != NULL) {
        (void)setvbuf(stream, NULL, _IONBF, 0);
    }
    return stream;
}
#elif defined(__GLIBC__)
static ssize_t dead_write(void *cookie, const char *data, size_t length)
{
    (void)cookie;
    (void)data;
    (void)length;
    errno = EIO;
    return 0;
}

static FILE *open_dead_stream(void)
{
    cookie_io_functions_t funcs = {.read = NULL, .write = dead_write, .seek = NULL, .close = NULL};
    FILE *stream = fopencookie(NULL, "w", funcs);

    if (stream != NULL) {
        (void)setvbuf(stream, NULL, _IONBF, 0);
    }
    return stream;
}
#else
/* No cookie stream here. tmpfile is not a dead stream; macOS and glibc do not
   use this branch. It exists so a config with neither symbol still typechecks. */
static FILE *open_dead_stream(void)
{
    return tmpfile();
}
#endif

static int save_env(const char *name, SavedEnv *slot)
{
    const char *old = getenv(name);

    slot->had = old != NULL;
    slot->saved = NULL;
    if (!slot->had) {
        return 0;
    }
    slot->saved = strdup(old);
    if (slot->saved == NULL) {
        return -1;
    }
    return 0;
}

static void restore_env(const char *name, SavedEnv *slot)
{
    if (slot->saved != NULL) {
        (void)setenv(name, slot->saved, 1);
    } else if (!slot->had) {
        (void)unsetenv(name);
    }
    free(slot->saved);
    slot->saved = NULL;
    slot->had = false;
}

static AppPresentation auto_presentation(void)
{
    AppPresentation mode = {
        .color = CLI_COLOR_AUTO,
        .quiet = false,
        .json = false,
        .pad_ = {'\0', '\0'},
    };

    return mode;
}

static void restore_presentation(void)
{
    app_set_presentation(auto_presentation());
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
}

/* The slave is a terminal. O_NOCTTY keeps it from becoming this process's
   controlling terminal. The caller fclose()s the result and close()s *master_fd. */
static FILE *open_pty_slave(int *master_fd, const char *mode)
{
    int master = -1;
    int slave_fd = -1;
    const char *name = NULL;
    FILE *slave = NULL;

    *master_fd = -1;
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        return NULL;
    }
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        (void)close(master);
        return NULL;
    }
    name = ptsname(master);
    if (name == NULL) {
        (void)close(master);
        return NULL;
    }
    slave_fd = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave_fd < 0) {
        (void)close(master);
        return NULL;
    }
    slave = fdopen(slave_fd, mode);
    if (slave == NULL) {
        (void)close(slave_fd);
        (void)close(master);
        return NULL;
    }
    *master_fd = master;
    return slave;
}

/* Progress is read back from the master. Turn off output processing so the
   terminal does not rewrite a newline as CR LF before the test sees it. */
static int slave_writes_raw(FILE *slave)
{
    struct termios mode;
    int descriptor = fileno(slave);

    if (descriptor < 0 || tcgetattr(descriptor, &mode) != 0) {
        return -1;
    }
    mode.c_oflag &= ~(tcflag_t)OPOST;
    if (tcsetattr(descriptor, TCSANOW, &mode) != 0) {
        return -1;
    }
    return 0;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) capacity, then the poll wait */
static int read_pty(int master_fd, char *buf, size_t cap, int wait_ms)
{
    struct pollfd ready;
    ssize_t nread = 0;
    size_t got = 0U;

    buf[0] = '\0';
    if (cap < 2U) {
        return -1;
    }
    ready.fd = master_fd;
    ready.events = POLLIN;
    if (poll(&ready, 1, wait_ms) <= 0) {
        return 0;
    }
    nread = read(master_fd, buf, cap - 1U);
    if (nread <= 0) {
        buf[0] = '\0';
        return nread < 0 ? -1 : 0;
    }
    got = (size_t)nread;
    buf[got] = '\0';
    return (int)got;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) stdout text, then stderr text */
static int run_captured(const char *db, const char *const *extra, size_t nextra, char **out_text,
                        char **err_text)
{
    const char *argv[CAPTURE_ARGV];
    size_t n = 0U;
    size_t i = 0U;
    char *out_buf = NULL;
    char *err_buf = NULL;
    size_t out_len = 0U;
    size_t err_len = 0U;
    FILE *out_fp = NULL;
    FILE *err_fp = NULL;
    int rc = 0;

    *out_text = NULL;
    *err_text = NULL;
    if (db == NULL || nextra + 3U > (size_t)CAPTURE_ARGV) {
        return -1;
    }
    argv[n++] = "remember";
    argv[n++] = "--db";
    argv[n++] = db;
    for (i = 0U; i < nextra; i++) {
        argv[n++] = extra[i];
    }
    out_fp = open_memstream(&out_buf, &out_len);
    err_fp = open_memstream(&err_buf, &err_len);
    if (out_fp == NULL || err_fp == NULL) {
        if (out_fp != NULL) {
            (void)fclose(out_fp);
        }
        if (err_fp != NULL) {
            (void)fclose(err_fp);
        }
        free(out_buf);
        free(err_buf);
        return -1;
    }
    rc = remember_run((int)n, (char *const *)argv, out_fp, err_fp);
    (void)fclose(out_fp);
    (void)fclose(err_fp);
    *out_text = out_buf;
    *err_text = err_buf;
    return rc;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) argument count, then the exit code */
static void assert_run(const char *db, const char *const *args, size_t n, int exit_code,
                       const char *out, const char *err)
{
    CmdResult r = run_remember(db, args, n, NULL);

    ASSERT_EQ_INT(r.exit_code, exit_code);
    ASSERT_STREQ(r.out, out);
    ASSERT_STREQ(r.err, err);
    cmd_result_free(&r);
}

/* typed NULL closes the master first, so the read sees EOF. "Ask: " is
   test-only text, not a product sentence. */
static int prompt_once(PromptAsk ask, PromptOut out)
{
    int master_fd = -1;
    FILE *slave = NULL;
    char *shown_buf = NULL;
    size_t shown_len = 0U;
    FILE *shown_fp = NULL;
    struct termios mode;
    int status = -1;
    ssize_t wrote = 0;

    out.text[0] = '\0';
    out.shown[0] = '\0';
    out.echo[0] = '\0';
    slave = open_pty_slave(&master_fd, "r");
    if (slave == NULL) {
        return -1;
    }
    if (tcgetattr(fileno(slave), &mode) != 0 || (mode.c_lflag & (unsigned)ECHO) == 0U) {
        goto cleanup;
    }
    shown_fp = open_memstream(&shown_buf, &shown_len);
    if (shown_fp == NULL) {
        goto cleanup;
    }
    app_set_streams((AppStreams){.out = NULL, .err = shown_fp});
    if (ask.typed != NULL) {
        size_t typed_len = strlen(ask.typed);

        wrote = write(master_fd, ask.typed, typed_len);
        if (wrote < 0 || (size_t)wrote != typed_len) {
            goto cleanup;
        }
    } else if (master_fd >= 0) {
        (void)close(master_fd);
        master_fd = -1;
    }
    status = app_prompt_line(slave, ask.prompt, out.text, ask.cap);
    (void)fflush(shown_fp);
    if (shown_buf != NULL && shown_len < out.shown_cap) {
        memcpy(out.shown, shown_buf, shown_len + 1U);
    }
    if (master_fd >= 0) {
        (void)read_pty(master_fd, out.echo, out.echo_cap, PTY_POLL_MS);
    }
    if (tcgetattr(fileno(slave), &mode) == 0 && out.echo_on != NULL) {
        *out.echo_on = ((mode.c_lflag & (unsigned)ECHO) != 0U) ? 1 : 0;
    }
cleanup:
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    if (shown_fp != NULL) {
        (void)fclose(shown_fp);
    }
    free(shown_buf);
    (void)fclose(slave);
    if (master_fd >= 0) {
        (void)close(master_fd);
    }
    return status;
}

static void progress_line(int master_fd, const char *line, long long now_ms, const char *expect)
{
    char got[PROGRESS_WIDE + 2];
    const char *want = expect != NULL ? expect : "";
    int wait_ms = want[0] == '\0' ? 0 : PTY_POLL_MS;

    got[0] = '\0';
    app_progress(line, now_ms);
    (void)read_pty(master_fd, got, sizeof got, wait_ms);
    ASSERT_STREQ(got, want);
}

/* The flags parse as globals. Version text stays plain. Bodies stay the
   caller's words. "applied" is not a product sentence. */
TEST(envelope_color_and_quiet_are_accepted_as_globals)
{
    static const char version_line[] = "remember 0.2.0\n";
    static const char invalid[] = "remember: invalid --color (must be auto, always, or never)\n";
    static const char missing[] = "remember: --color: option requires a value\n";
    static const char json_invalid[] =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"unknown.result\",\"error\":{"
        "\"code\":\"usage\",\"message\":\"invalid --color (must be auto, always, or never)\","
        "\"fix\":\"Run 'remember --help' to see the commands.\",\"retryable\":false}}\n";
    char *db = make_temp_db_path();
    CliArgs args;
    char p0[] = "remember";
    char p1[] = "--color=always";
    char p2[] = "--quiet";
    char p3[] = "--color=never";
    char p4[] = "add";
    char p5[] = "hello-color";
    char *parsed[] = {p0, p1, p2, p3, p4, p5};
    char d0[] = "remember";
    char d1[] = "add";
    char d2[] = "--";
    char d3[] = "--quiet";
    char *dashed[] = {d0, d1, d2, d3};
    char s0[] = "remember";
    char s1[] = "--color";
    char s2[] = "never";
    char s3[] = "version";
    char *spaced[] = {s0, s1, s2, s3};
    const char *never[] = {"--color=never", "--quiet", "version"};
    const char *always[] = {"--color=always", "version"};
    const char *after[] = {"version", "--color=auto", "--quiet"};
    const char *json_ok[] = {"--json", "--color=always", "--quiet", "version"};
    const char *rainbow[] = {"--color=rainbow", "version"};
    const char *upper[] = {"--color=AUTO", "version"};
    const char *longer[] = {"--color=nevermore", "version"};
    const char *json_bad[] = {"--json", "--color=rainbow", "version"};
    const char *bare[] = {"--color"};
    const char *empty[] = {"--color=", "version"};
    const char *looks[] = {"--color", "--quiet", "version"};
    const char *dashed_value[] = {"--color", "-never", "version"};
    const char *space_ok[] = {"--color", "never", "version"};
    const char *flag_after[] = {"version", "--color=never"};
    const char *quiet_eq[] = {"--quiet=1", "version"};
    const char *colorful[] = {"--colorful", "version"};
    const char *add[] = {"add", "--color=never", "--quiet", "hello-color"};
    const char *get_body[] = {"get", "1"};
    const char *literal[] = {"add", "--", "--quiet"};
    const char *get_literal[] = {"get", "2"};

    cli_parse((int)(sizeof parsed / sizeof parsed[0]), parsed, &args);
    ASSERT_EQ_INT((int)args.error, (int)CLI_ERR_OK);
    ASSERT_EQ_INT((int)args.globals.color, (int)CLI_COLOR_NEVER);
    ASSERT_TRUE(args.globals.quiet);
    ASSERT_EQ_INT(args.rest_argc, 1);
    if (args.rest_argc == 1) {
        ASSERT_STREQ(args.rest_argv[0], "hello-color");
    }
    cli_args_free(&args);

    cli_parse(4, dashed, &args);
    ASSERT_EQ_INT((int)args.error, (int)CLI_ERR_OK);
    ASSERT_FALSE(args.globals.quiet);
    ASSERT_EQ_INT((int)args.globals.color, (int)CLI_COLOR_AUTO);
    ASSERT_EQ_INT(args.rest_argc, 2);
    if (args.rest_argc == 2) {
        ASSERT_STREQ(args.rest_argv[0], "--");
        ASSERT_STREQ(args.rest_argv[1], "--quiet");
    }
    cli_args_free(&args);

    cli_parse(4, spaced, &args);
    ASSERT_EQ_INT((int)args.error, (int)CLI_ERR_OK);
    ASSERT_EQ_INT((int)args.globals.color, (int)CLI_COLOR_NEVER);
    ASSERT_EQ_INT((int)args.command, (int)CLI_CMD_VERSION);
    cli_args_free(&args);

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    assert_run(db, never, sizeof never / sizeof never[0], 0, version_line, "");
    assert_run(db, always, sizeof always / sizeof always[0], 0, version_line, "");
    assert_run(db, after, sizeof after / sizeof after[0], 0, version_line, "");
    assert_run(db, json_ok, sizeof json_ok / sizeof json_ok[0], 0, version_line, "");
    assert_run(db, rainbow, sizeof rainbow / sizeof rainbow[0], 1, "", invalid);
    assert_run(db, upper, sizeof upper / sizeof upper[0], 1, "", invalid);
    assert_run(db, longer, sizeof longer / sizeof longer[0], 1, "", invalid);
    assert_run(db, json_bad, sizeof json_bad / sizeof json_bad[0], 1, json_invalid, "");
    assert_run(db, bare, sizeof bare / sizeof bare[0], 1, "", missing);
    assert_run(db, empty, sizeof empty / sizeof empty[0], 1, "", missing);
    assert_run(db, looks, sizeof looks / sizeof looks[0], 1, "", missing);
    assert_run(db, dashed_value, sizeof dashed_value / sizeof dashed_value[0], 1, "", missing);
    assert_run(db, space_ok, sizeof space_ok / sizeof space_ok[0], 0, version_line, "");
    assert_run(db, flag_after, sizeof flag_after / sizeof flag_after[0], 0, version_line, "");
    assert_run(db, quiet_eq, sizeof quiet_eq / sizeof quiet_eq[0], 1, "",
               "remember: unknown option '--quiet=1'\n");
    assert_run(db, colorful, sizeof colorful / sizeof colorful[0], 1, "",
               "remember: unknown option '--colorful'\n");
    assert_run(db, add, sizeof add / sizeof add[0], 0, "1\n", "");
    assert_run(db, get_body, sizeof get_body / sizeof get_body[0], 0, "hello-color\n", "");
    assert_run(db, literal, sizeof literal / sizeof literal[0], 0, "2\n", "");
    assert_run(db, get_literal, sizeof get_literal / sizeof get_literal[0], 0, "--quiet\n", "");
    free(db);
}

TEST(envelope_color_follows_the_tty_and_the_environment)
{
    SavedEnv no_color = {0};
    SavedEnv term = {0};
    int master_fd = -1;
    FILE *tty = NULL;
    FILE *plain = NULL;
    FILE *broken_fp = NULL;
    char *db = NULL;
    AppPresentation mode;
    const char *always_args[] = {"--color=always", "version"};
    const char *auto_args[] = {"version"};
    char *out_text = NULL;
    char *err_text = NULL;
    int rc = 0;

    ASSERT_EQ_INT(save_env("NO_COLOR", &no_color), 0);
    ASSERT_EQ_INT(save_env("TERM", &term), 0);
    if ((no_color.had && no_color.saved == NULL) || (term.had && term.saved == NULL)) {
        goto cleanup;
    }
    tty = open_pty_slave(&master_fd, "r");
    plain = tmpfile();
    broken_fp = open_dead_stream();
    if (tty == NULL || plain == NULL || broken_fp == NULL) {
        ASSERT_TRUE(tty != NULL);
        ASSERT_TRUE(plain != NULL);
        ASSERT_TRUE(broken_fp != NULL);
        goto cleanup;
    }

    mode = auto_presentation();
    mode.color = CLI_COLOR_ALWAYS;
    app_set_presentation(mode);
    ASSERT_EQ_INT(setenv("NO_COLOR", "1", 1), 0);
    ASSERT_EQ_INT(setenv("TERM", "dumb", 1), 0);
    ASSERT_TRUE(app_use_color(plain));
    ASSERT_TRUE(app_use_color(NULL));

    mode.color = CLI_COLOR_NEVER;
    app_set_presentation(mode);
    ASSERT_EQ_INT(unsetenv("NO_COLOR"), 0);
    ASSERT_EQ_INT(setenv("TERM", "xterm", 1), 0);
    ASSERT_FALSE(app_use_color(tty));
    ASSERT_FALSE(app_use_color(NULL));

    mode.color = CLI_COLOR_AUTO;
    app_set_presentation(mode);
    ASSERT_TRUE(app_use_color(tty));
    ASSERT_EQ_INT(setenv("NO_COLOR", "", 1), 0);
    ASSERT_FALSE(app_use_color(tty));
    ASSERT_EQ_INT(setenv("NO_COLOR", "1", 1), 0);
    ASSERT_FALSE(app_use_color(tty));
    ASSERT_EQ_INT(unsetenv("NO_COLOR"), 0);
    ASSERT_EQ_INT(setenv("TERM", "dumb", 1), 0);
    ASSERT_FALSE(app_use_color(tty));
    ASSERT_EQ_INT(unsetenv("TERM"), 0);
    ASSERT_TRUE(app_use_color(tty));
    ASSERT_EQ_INT(setenv("TERM", "Dumb", 1), 0);
    ASSERT_TRUE(app_use_color(tty));
    ASSERT_EQ_INT(setenv("TERM", "", 1), 0);
    ASSERT_TRUE(app_use_color(tty));
    ASSERT_EQ_INT(setenv("TERM", "xterm", 1), 0);
    ASSERT_FALSE(app_use_color(plain));
    ASSERT_FALSE(app_use_color(NULL));
    ASSERT_FALSE(app_use_color(broken_fp));

    db = make_temp_db_path();
    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        goto cleanup;
    }
    ASSERT_EQ_INT(setenv("NO_COLOR", "1", 1), 0);
    ASSERT_EQ_INT(setenv("TERM", "dumb", 1), 0);
    rc = run_captured(db, always_args, sizeof always_args / sizeof always_args[0], &out_text,
                      &err_text);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(out_text, "remember 0.2.0\n");
    ASSERT_STREQ(err_text, "");
    ASSERT_TRUE(app_use_color(plain));
    free(out_text);
    free(err_text);
    out_text = NULL;
    err_text = NULL;
    rc = run_captured(db, auto_args, sizeof auto_args / sizeof auto_args[0], &out_text, &err_text);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_FALSE(app_use_color(plain));
    ASSERT_FALSE(app_use_color(tty));
    free(out_text);
    free(err_text);

cleanup:
    restore_presentation();
    restore_env("NO_COLOR", &no_color);
    restore_env("TERM", &term);
    if (broken_fp != NULL) {
        (void)fclose(broken_fp);
    }
    if (plain != NULL) {
        (void)fclose(plain);
    }
    if (tty != NULL) {
        (void)fclose(tty);
    }
    if (master_fd >= 0) {
        (void)close(master_fd);
    }
    free(db);
}

/* "applied" and "failed" are the H-16.10 examples passed into the writer.
   They are not product copy. */
TEST(envelope_progress_is_a_stderr_line_and_quiet_json_stop_it)
{
    int master_fd = -1;
    int dead_master = -1;
    FILE *slave = NULL;
    FILE *dead_slave = NULL;
    FILE *plain = NULL;
    char *out_buf = NULL;
    size_t out_len = 0U;
    FILE *out_fp = NULL;
    char *db = NULL;
    char wide[PROGRESS_WIDE + 1];
    char wide_expect[PROGRESS_WIDE + 2];
    AppPresentation mode;
    const char *quiet_args[] = {"--quiet", "version"};
    const char *plain_args[] = {"version"};
    const char *json_args[] = {"--json", "version"};
    char *out_text = NULL;
    char *err_text = NULL;
    int rc = 0;

    memset(wide, 'a', sizeof wide);
    wide[PROGRESS_WIDE] = '\0';
    memset(wide_expect, 'a', PROGRESS_WIDE);
    wide_expect[PROGRESS_WIDE] = '\n';
    wide_expect[PROGRESS_WIDE + 1] = '\0';

    slave = open_pty_slave(&master_fd, "w");
    if (slave != NULL && slave_writes_raw(slave) != 0) {
        (void)fclose(slave);
        slave = NULL;
        if (master_fd >= 0) {
            (void)close(master_fd);
            master_fd = -1;
        }
    }
    plain = tmpfile();
    out_fp = open_memstream(&out_buf, &out_len);
    if (slave == NULL || plain == NULL || out_fp == NULL) {
        ASSERT_TRUE(slave != NULL);
        ASSERT_TRUE(plain != NULL);
        ASSERT_TRUE(out_fp != NULL);
        goto cleanup;
    }
    app_set_streams((AppStreams){.out = out_fp, .err = slave});
    app_set_presentation(auto_presentation());
    progress_line(master_fd, "applied", 0, "applied\n");
    progress_line(master_fd, "applied", PROGRESS_GAP_MS - 1, "");
    progress_line(master_fd, "failed", PROGRESS_GAP_MS, "failed\n");
    progress_line(master_fd, "applied", (long long)PROGRESS_GAP_MS * PROGRESS_DUE_GAPS,
                  "applied\n");
    progress_line(master_fd, "nope", (long long)PROGRESS_GAP_MS * PROGRESS_REWIND_GAPS, "");
    progress_line(master_fd, "later", (long long)PROGRESS_GAP_MS * PROGRESS_EXACT_GAPS, "later\n");

    app_set_presentation(auto_presentation());
    progress_line(master_fd, NULL, 0, "");
    progress_line(master_fd, "", 0, "");
    app_set_presentation(auto_presentation());
    progress_line(master_fd, "applied\x1b[2K", 0, "applied?[2K\n");
    app_set_presentation(auto_presentation());
    progress_line(master_fd, "spin\r", 0, "spin?\n");
    app_set_presentation(auto_presentation());
    progress_line(master_fd, "one\ntwo", 0, "one?two\n");
    app_set_presentation(auto_presentation());
    progress_line(master_fd, wide, 0, wide_expect);

    mode = auto_presentation();
    mode.color = CLI_COLOR_ALWAYS;
    app_set_presentation(mode);
    progress_line(master_fd, "applied", 0, "applied\n");

    mode.quiet = true;
    app_set_presentation(mode);
    progress_line(master_fd, "applied", 0, "");
    mode.quiet = false;
    mode.json = true;
    app_set_presentation(mode);
    progress_line(master_fd, "applied", 0, "");

    mode.json = false;
    mode.color = CLI_COLOR_ALWAYS;
    app_set_presentation(mode);
    app_set_streams((AppStreams){.out = out_fp, .err = plain});
    app_progress("applied", 0);
    ASSERT_EQ_INT(fflush(plain), 0);
    ASSERT_EQ_INT((int)ftell(plain), 0);

    dead_slave = open_pty_slave(&dead_master, "w");
    ASSERT_TRUE(dead_slave != NULL);
    if (dead_slave != NULL) {
        (void)setvbuf(dead_slave, NULL, _IONBF, 0);
        (void)close(dead_master);
        dead_master = -1;
        app_set_streams((AppStreams){.out = out_fp, .err = dead_slave});
        app_set_presentation(auto_presentation());
        app_progress("applied", 0);
        app_set_streams((AppStreams){.out = out_fp, .err = slave});
        progress_line(master_fd, "applied", 0, "applied\n");
        (void)fclose(dead_slave);
        dead_slave = NULL;
    }

    db = make_temp_db_path();
    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        goto cleanup;
    }
    rc = run_captured(db, quiet_args, sizeof quiet_args / sizeof quiet_args[0], &out_text,
                      &err_text);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(out_text, "remember 0.2.0\n");
    ASSERT_STREQ(err_text, "");
    free(out_text);
    free(err_text);
    app_set_streams((AppStreams){.out = out_fp, .err = slave});
    progress_line(master_fd, "applied", 0, "");

    rc = run_captured(db, plain_args, sizeof plain_args / sizeof plain_args[0], &out_text,
                      &err_text);
    ASSERT_EQ_INT(rc, 0);
    free(out_text);
    free(err_text);
    app_set_streams((AppStreams){.out = out_fp, .err = slave});
    progress_line(master_fd, "applied", 0, "applied\n");

    rc = run_captured(db, json_args, sizeof json_args / sizeof json_args[0], &out_text, &err_text);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(out_text, "remember 0.2.0\n");
    ASSERT_STREQ(err_text, "");
    free(out_text);
    free(err_text);
    app_set_streams((AppStreams){.out = out_fp, .err = slave});
    progress_line(master_fd, "applied", 0, "");

    ASSERT_EQ_INT(fflush(out_fp), 0);
    ASSERT_STREQ(out_buf, "");

cleanup:
    restore_presentation();
    if (dead_slave != NULL) {
        (void)fclose(dead_slave);
    }
    if (dead_master >= 0) {
        (void)close(dead_master);
    }
    if (out_fp != NULL) {
        (void)fclose(out_fp);
    }
    free(out_buf);
    if (plain != NULL) {
        (void)fclose(plain);
    }
    if (slave != NULL) {
        (void)fclose(slave);
    }
    if (master_fd >= 0) {
        (void)close(master_fd);
    }
    free(db);
}

TEST(envelope_prompt_echoes_the_typed_line)
{
    char text[PROMPT_SLOT];
    char shown[PROMPT_SLOT];
    char echo[PROMPT_SLOT];
    int echo_on = 0;
    PromptOut out = {0};
    int ends[2] = {-1, -1};
    FILE *input = NULL;
    FILE *sink_fp = NULL;
    char buf[PROMPT_SLOT];
    char left[PROMPT_SLOT];
    size_t got = 0U;
    ssize_t wrote = 0;
    PromptAsk ask;

    text[0] = 'x';
    ASSERT_EQ_INT(app_prompt_line(NULL, "Ask: ", text, sizeof text), -1);
    ASSERT_STREQ(text, "");
    ASSERT_EQ_INT(app_prompt_line(NULL, "Ask: ", NULL, sizeof text), -1);
    text[0] = 'x';
    ASSERT_EQ_INT(app_prompt_line(NULL, "Ask: ", text, 1), -1);
    ASSERT_STREQ(text, "");
    text[0] = 'x';
    ASSERT_EQ_INT(app_prompt_line(NULL, "Ask: ", text, 0), -1);
    ASSERT_EQ_INT(text[0], (int)'x');

    out.text = text;
    out.text_cap = sizeof text;
    out.shown = shown;
    out.shown_cap = sizeof shown;
    out.echo = echo;
    out.echo_cap = sizeof echo;
    out.echo_on = &echo_on;

    ask.typed = "yes\n";
    ask.prompt = "Ask: ";
    ask.cap = sizeof text;
    ASSERT_EQ_INT(prompt_once(ask, out), 0);
    ASSERT_STREQ(text, "yes");
    ASSERT_STREQ(shown, "Ask: ");
    ASSERT_STR_CONTAINS(echo, "yes");
    ASSERT_STR_CONTAINS(echo, "\n");
    ASSERT_STR_NOT_CONTAINS(shown, "yes");
    ASSERT_EQ_INT(echo_on, 1);

    ask.typed = "\n";
    ask.prompt = "";
    ASSERT_EQ_INT(prompt_once(ask, out), 0);
    ASSERT_STREQ(text, "");
    ASSERT_STREQ(shown, "");

    ask.typed = "z\n";
    ask.prompt = NULL;
    ASSERT_EQ_INT(prompt_once(ask, out), 0);
    ASSERT_STREQ(text, "z");
    ASSERT_STREQ(shown, "");

    ask.typed = "ok\n";
    ask.prompt = "\r\x1b";
    ASSERT_EQ_INT(prompt_once(ask, out), 0);
    ASSERT_STREQ(text, "ok");
    ASSERT_STREQ(shown, "??");

    ask.typed = "yes\n";
    ask.prompt = "Ask: ";
    ask.cap = PROMPT_TINY;
    ASSERT_EQ_INT(prompt_once(ask, out), -1);
    ASSERT_STREQ(text, "");

    ask.typed = NULL;
    ask.cap = sizeof text;
    ASSERT_EQ_INT(prompt_once(ask, out), -1);
    ASSERT_STREQ(text, "");

    // NOLINTNEXTLINE(android-cloexec-pipe)
    ASSERT_EQ_INT(pipe(ends), 0);
    if (ends[0] < 0) {
        return;
    }
    wrote = write(ends[1], "yes\n", 4);
    ASSERT_EQ_INT((int)wrote, 4);
    if (wrote != 4) {
        goto cleanup;
    }
    (void)close(ends[1]);
    ends[1] = -1;
    input = fdopen(ends[0], "r");
    if (input != NULL) {
        ends[0] = -1;
    }
    sink_fp = open_dead_stream();
    if (input == NULL || sink_fp == NULL) {
        ASSERT_TRUE(input != NULL);
        ASSERT_TRUE(sink_fp != NULL);
        goto cleanup;
    }
    app_set_streams((AppStreams){.out = NULL, .err = sink_fp});
    buf[0] = 'x';
    ASSERT_EQ_INT(app_prompt_line(input, "Ask: ", buf, sizeof buf), -1);
    ASSERT_STREQ(buf, "");
    got = fread(left, 1, 4, input);
    left[got] = '\0';
    ASSERT_EQ_INT((int)got, 4);
    ASSERT_STREQ(left, "yes\n");

cleanup:
    restore_presentation();
    if (sink_fp != NULL) {
        (void)fclose(sink_fp);
    }
    if (input != NULL) {
        (void)fclose(input);
    }
    if (ends[0] >= 0) {
        (void)close(ends[0]);
    }
    if (ends[1] >= 0) {
        (void)close(ends[1]);
    }
}

// NOLINTEND(concurrency-mt-unsafe)

/* Join parts in order. The caller frees. NULL when the allocation fails. */
static char *join_parts(const char *const *parts, size_t n)
{
    size_t i = 0U;
    size_t total = 0U;
    char *buf = NULL;
    char *at = NULL;

    for (i = 0U; i < n; i++) {
        total += strlen(parts[i]);
    }
    buf = (char *)malloc(total + 1U);
    if (buf == NULL) {
        return NULL;
    }
    at = buf;
    for (i = 0U; i < n; i++) {
        size_t len = strlen(parts[i]);

        memcpy(at, parts[i], len);
        at += len;
    }
    *at = '\0';
    return buf;
}

/* C-35 file line. The same path bytes, not the same pointer, warn once. */
TEST(envelope_unknown_field_warns_once_for_a_file)
{
    char path_a[] = "/tmp/note.json";
    char path_b[] = "/tmp/note.json";
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = path_a, .name = "extra"}), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = path_b, .name = "other"}), 0);
    ASSERT_EQ_INT(diag_warning_count(), 1U);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: \"/tmp/note.json\" has a field remember doesn't know, "
                      "\"extra\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* C-35 stdin line. A file in the same stretch still warns. */
TEST(envelope_unknown_field_warns_once_for_stdin)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = NULL, .name = "extra"}), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = NULL, .name = "other"}), 0);
    ASSERT_EQ_INT(
        diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "filefield"}), 0);
    ASSERT_EQ_INT(diag_warning_count(), 2U);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: stdin has a field remember doesn't know, \"extra\". "
                      "The rest of the input was read.\n"
                      "remember: warning: \"/tmp/note.json\" has a field remember doesn't know, "
                      "\"filefield\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* Each path warns once. A later field on the first or the second is silent. */
TEST(envelope_unknown_field_warns_once_per_path)
{
    char first[] = "/tmp/a.json";
    char first_copy[] = "/tmp/a.json";
    char second[] = "/tmp/b.json";
    char second_copy[] = "/tmp/b.json";
    char third[] = "/tmp/c.json";
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = first, .name = "extra"}), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = second, .name = "extra"}), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = first_copy, .name = "other"}), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = second_copy, .name = "other"}), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = third, .name = "extra"}), 0);
    ASSERT_EQ_INT(diag_warning_count(), 3U);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: \"/tmp/a.json\" has a field remember doesn't know, "
                      "\"extra\". The rest of the file was read.\n"
                      "remember: warning: \"/tmp/b.json\" has a field remember doesn't know, "
                      "\"extra\". The rest of the file was read.\n"
                      "remember: warning: \"/tmp/c.json\" has a field remember doesn't know, "
                      "\"extra\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* diag_warnings_reset clears the once-per-file set, so the next run can warn. */
TEST(envelope_unknown_field_warns_again_after_a_reset)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "extra"}),
                  0);
    diag_warnings_reset();
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "extra"}),
                  0);
    ASSERT_EQ_INT(diag_warning_count(), 1U);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: \"/tmp/note.json\" has a field remember doesn't know, "
                      "\"extra\". The rest of the file was read.\n"
                      "remember: warning: \"/tmp/note.json\" has a field remember doesn't know, "
                      "\"extra\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* JSON code, message, and detail. A second field in the same file adds nothing. */
TEST(envelope_unknown_field_json_names_the_field_and_the_file)
{
    char *err_buf = NULL;
    char *out_buf = NULL;
    size_t err_len = 0U;
    size_t out_len = 0U;
    FILE *err = open_memstream(&err_buf, &err_len);
    FILE *out = open_memstream(&out_buf, &out_len);

    ASSERT_TRUE(err != NULL);
    ASSERT_TRUE(out != NULL);
    if (err == NULL || out == NULL) {
        if (err != NULL) {
            (void)fclose(err);
        }
        if (out != NULL) {
            (void)fclose(out);
        }
        free(err_buf);
        free(out_buf);
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_json_begin(NULL), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "extra"}),
                  0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "other"}),
                  0);
    ASSERT_EQ_INT(diag_warning_count(), 1U);
    ASSERT_EQ_INT(diag_json_write_warnings(out), 0);
    diag_json_end();
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_EQ_INT(fflush(out), 0);
    ASSERT_STREQ(err_buf, "");
    ASSERT_STREQ(out_buf,
                 ",\"warnings\":[{\"code\":\"unknown_field\",\"message\":\"\\\"/tmp/note.json\\\" "
                 "has a field remember doesn't know, \\\"extra\\\". The rest of the file was "
                 "read.\",\"detail\":{\"field\":\"extra\",\"where\":\"/tmp/note.json\"}}]");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    ASSERT_EQ_INT(fclose(out), 0);
    free(err_buf);
    free(out_buf);
}

/* JSON where for a stream is stdin, and the message is the stream sentence. */
TEST(envelope_unknown_field_json_names_stdin)
{
    char *err_buf = NULL;
    char *out_buf = NULL;
    size_t err_len = 0U;
    size_t out_len = 0U;
    FILE *err = open_memstream(&err_buf, &err_len);
    FILE *out = open_memstream(&out_buf, &out_len);

    ASSERT_TRUE(err != NULL);
    ASSERT_TRUE(out != NULL);
    if (err == NULL || out == NULL) {
        if (err != NULL) {
            (void)fclose(err);
        }
        if (out != NULL) {
            (void)fclose(out);
        }
        free(err_buf);
        free(out_buf);
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_json_begin(NULL), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = NULL, .name = "extra"}), 0);
    ASSERT_EQ_INT(diag_warning_count(), 1U);
    ASSERT_EQ_INT(diag_json_write_warnings(out), 0);
    diag_json_end();
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_EQ_INT(fflush(out), 0);
    ASSERT_STREQ(err_buf, "");
    ASSERT_STREQ(out_buf, ",\"warnings\":[{\"code\":\"unknown_field\",\"message\":\"stdin has a "
                          "field remember doesn't know, \\\"extra\\\". The rest of the input was "
                          "read.\",\"detail\":{\"field\":\"extra\",\"where\":\"stdin\"}}]");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    ASSERT_EQ_INT(fclose(out), 0);
    free(err_buf);
    free(out_buf);
}

/* Hostile bytes stay one plain line. JSON keeps the real escapes, not '?'. */
enum { HOSTILE_PAD = 300 };

TEST(envelope_unknown_field_masks_the_plain_line_and_escapes_json)
{
    const char raw_head[] = "\n\"\x1b\x7f\t\xc2\x9b";
    const char plain_head[] = "?\"??\t?";
    const char json_head[] = "\\n\\\"\\u001b\x7f\\t\xc2\x9b";
    char probe[sizeof raw_head + HOSTILE_PAD];
    char masked[sizeof plain_head + HOSTILE_PAD];
    char jprobe[sizeof json_head + HOSTILE_PAD];
    const char *plain_parts[] = {"remember: warning: \"", masked,
                                 "\" has a field remember doesn't know, \"", masked,
                                 "\". The rest of the file was read.\n"};
    const char *json_parts[] = {",\"warnings\":[{\"code\":\"unknown_field\",\"message\":\"\\\"",
                                jprobe,
                                "\\\" has a field remember doesn't know, \\\"",
                                jprobe,
                                "\\\". The rest of the file was read.\",\"detail\":{\"field\":\"",
                                jprobe,
                                "\",\"where\":\"",
                                jprobe,
                                "\"}}]"};
    char *plain_expect = NULL;
    char *json_expect = NULL;
    char *err_buf = NULL;
    char *out_buf = NULL;
    size_t err_len = 0U;
    size_t out_len = 0U;
    FILE *err = NULL;
    FILE *out = NULL;
    size_t plain_n = 0U;

    memcpy(probe, raw_head, sizeof raw_head - 1U);
    memset(probe + (sizeof raw_head - 1U), 'a', HOSTILE_PAD);
    probe[sizeof raw_head - 1U + HOSTILE_PAD] = '\0';
    memcpy(masked, plain_head, sizeof plain_head - 1U);
    memset(masked + (sizeof plain_head - 1U), 'a', HOSTILE_PAD);
    masked[sizeof plain_head - 1U + HOSTILE_PAD] = '\0';
    memcpy(jprobe, json_head, sizeof json_head - 1U);
    memset(jprobe + (sizeof json_head - 1U), 'a', HOSTILE_PAD);
    jprobe[sizeof json_head - 1U + HOSTILE_PAD] = '\0';
    plain_expect = join_parts(plain_parts, sizeof plain_parts / sizeof plain_parts[0]);
    json_expect = join_parts(json_parts, sizeof json_parts / sizeof json_parts[0]);
    err = open_memstream(&err_buf, &err_len);
    ASSERT_TRUE(plain_expect != NULL);
    ASSERT_TRUE(json_expect != NULL);
    ASSERT_TRUE(err != NULL);
    if (plain_expect == NULL || json_expect == NULL || err == NULL) {
        free(plain_expect);
        free(json_expect);
        if (err != NULL) {
            (void)fclose(err);
        }
        free(err_buf);
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = probe, .name = probe}), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(err_buf, plain_expect);
    plain_n = strlen(err_buf);
    ASSERT_TRUE(plain_n > 0U && err_buf[plain_n - 1U] == '\n');
    ASSERT_TRUE(memchr(err_buf, '\n', plain_n - 1U) == NULL);
    ASSERT_TRUE(memchr(err_buf, '\x1b', plain_n) == NULL);
    ASSERT_TRUE(memchr(err_buf, '\x7f', plain_n) == NULL);
    ASSERT_TRUE(memchr(err_buf, (int)(unsigned char)'\xc2', plain_n) == NULL);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(err_buf);
    err_buf = NULL;
    diag_warnings_reset();

    err_len = 0U;
    err = open_memstream(&err_buf, &err_len);
    out = open_memstream(&out_buf, &out_len);
    ASSERT_TRUE(err != NULL);
    ASSERT_TRUE(out != NULL);
    if (err == NULL || out == NULL) {
        if (err != NULL) {
            (void)fclose(err);
        }
        if (out != NULL) {
            (void)fclose(out);
        }
        free(err_buf);
        free(out_buf);
        free(plain_expect);
        free(json_expect);
        diag_warnings_reset();
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_json_begin(NULL), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = probe, .name = probe}), 0);
    ASSERT_EQ_INT(diag_json_write_warnings(out), 0);
    diag_json_end();
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_EQ_INT(fflush(out), 0);
    ASSERT_STREQ(err_buf, "");
    ASSERT_STREQ(out_buf, json_expect);
    ASSERT_TRUE(memchr(out_buf, '\n', out_len) == NULL);
    ASSERT_TRUE(memchr(out_buf, '\x1b', out_len) == NULL);
    ASSERT_TRUE(memchr(out_buf, '\x7f', out_len) != NULL);
    ASSERT_TRUE(memchr(out_buf, (int)(unsigned char)'\x9b', out_len) != NULL);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    ASSERT_EQ_INT(fclose(out), 0);
    free(err_buf);
    free(out_buf);
    free(plain_expect);
    free(json_expect);
}

/* A NULL name writes nothing and does not consume the file or stdin. */
TEST(envelope_unknown_field_rejects_a_null_name)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = NULL, .name = NULL}), -1);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = NULL}),
                  -1);
    ASSERT_EQ_INT(diag_warning_count(), 0U);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "");
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = NULL, .name = "extra"}), 0);
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "extra"}),
                  0);
    ASSERT_EQ_INT(diag_warning_count(), 2U);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* An empty name is a real key. The sentence quotes it as "". */
TEST(envelope_unknown_field_quotes_an_empty_name)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/empty.json", .name = ""}), 0);
    ASSERT_EQ_INT(diag_warning_count(), 1U);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: \"/tmp/empty.json\" has a field remember doesn't know, "
                      "\"\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

void register_envelope_tests(void)
{
    RUN_TEST(envelope_get_by_digits_and_by_uuid_is_the_same_memory);
    RUN_TEST(envelope_uuid_positional_updates_deletes_and_links);
    RUN_TEST(envelope_positional_rejects_what_is_not_a_memory_id);
    RUN_TEST(envelope_delete_expired_by_uuid_is_a_soft_delete);
    RUN_TEST(envelope_memory_id_is_the_uuid_and_the_old_flags_are_unknown);
    RUN_TEST(envelope_get_head_is_format_version_type_then_data_memory);
    RUN_TEST(envelope_add_leaves_out_a_neighbor_in_the_trash_like_get);
    RUN_TEST(envelope_a_new_memory_has_created_by_and_updated_by_and_no_source);
    RUN_TEST(envelope_shape_checker_reads_only_top_level_keys);
    RUN_TEST(envelope_every_command_names_its_reply_type);
    RUN_TEST(envelope_failure_keeps_its_type_and_carries_the_warnings_after_the_error);
    RUN_TEST(envelope_warnings_keep_their_order_with_a_comma_between);
    RUN_TEST(envelope_trash_is_the_deleted_bin);
    RUN_TEST(envelope_strict_makes_a_trash_warning_exit_4);
    RUN_TEST(envelope_help_states_trash_and_expired);
    RUN_TEST(envelope_update_trash_undelete_restores);
    RUN_TEST(envelope_keyless_wrong_list_uses_the_uuid);
    RUN_TEST(envelope_blank_id_wrong_list_keeps_the_deleted_token);
    RUN_TEST(envelope_trash_and_expired_cannot_combine);
    RUN_TEST(envelope_purge_trash_selects_the_deleted_bin);
    RUN_TEST(envelope_brain_new_id_replaces_only_the_brain_id);
    RUN_TEST(envelope_help_prints_the_color_and_quiet_lines);
    RUN_TEST(envelope_fixed_ids_make_replies_predictable);
    RUN_TEST(envelope_fixed_ids_ignore_a_bad_value);
    RUN_TEST(envelope_fixed_ids_are_ignored_outside_the_test_cli);
    RUN_TEST(envelope_every_write_accepts_the_hook_flags);
    RUN_TEST(envelope_color_and_quiet_are_accepted_as_globals);
    RUN_TEST(envelope_color_follows_the_tty_and_the_environment);
    RUN_TEST(envelope_progress_is_a_stderr_line_and_quiet_json_stop_it);
    RUN_TEST(envelope_prompt_echoes_the_typed_line);
    RUN_TEST(envelope_unknown_field_warns_once_for_a_file);
    RUN_TEST(envelope_unknown_field_warns_once_for_stdin);
    RUN_TEST(envelope_unknown_field_warns_once_per_path);
    RUN_TEST(envelope_unknown_field_warns_again_after_a_reset);
    RUN_TEST(envelope_unknown_field_json_names_the_field_and_the_file);
    RUN_TEST(envelope_unknown_field_json_names_stdin);
    RUN_TEST(envelope_unknown_field_masks_the_plain_line_and_escapes_json);
    RUN_TEST(envelope_unknown_field_rejects_a_null_name);
    RUN_TEST(envelope_unknown_field_quotes_an_empty_name);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(envelope_add_link_read_error_after_the_write_exits_1_and_keeps_the_write);
#else
    (void)printf("SKIP the envelope VFS shim test (remember built without REMEMBER_TEST_VFS)\n");
#endif
}
