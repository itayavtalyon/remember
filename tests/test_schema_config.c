#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* mkdtemp: Darwin declares it in unistd.h; glibc often via stdlib + POSIX. */
#if defined(__APPLE__)
#include <unistd.h>
#endif

/*
 * Schema version + sync-path warning (design Round 7).
 * user_version>1 refusal needs a crafted DB; we use the sqlite3 CLI if present,
 * otherwise skip-style soft assert via a helper that still fails until impl.
 */

TEST(sync_path_warning_on_clouddocs_marker)
{
    /*
     * Path containing com~apple~CloudDocs must warn on stderr and still work
     * once implemented. Skeleton fails until then.
     */
    char *db = make_temp_db_path();
    char *syncish = NULL;
    CmdResult r;
    const char *a[] = {"add", "cloud path note"};
    size_t n;
    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    /* Build a path that embeds the marker but lives under /tmp for safety. */
    n = strlen("/tmp/com~apple~CloudDocs-remember-XXXXXX") + 1U;
    syncish = malloc(n);
    ASSERT_TRUE(syncish != NULL);
    if (syncish == NULL) {
        free(db);
        return;
    }
    (void)snprintf(syncish, n, "%s", "/tmp/com~apple~CloudDocs-remember-XXXXXX");
    if (mkdtemp(syncish) == NULL) {
        free(syncish);
        free(db);
        ASSERT_TRUE(0);
        return;
    }
    {
        char *path = NULL;
        size_t pn = strlen(syncish) + strlen("/t.db") + 1U;
        path = malloc(pn);
        ASSERT_TRUE(path != NULL);
        if (path == NULL) {
            free(syncish);
            free(db);
            return;
        }
        (void)snprintf(path, pn, "%s/t.db", syncish);
        r = run_remember(path, a, 2, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        /* Heuristic warning required (design: one-line stderr). */
        ASSERT_TRUE(r.err != NULL && r.err[0] != '\0');
        if (r.err != NULL) {
            ASSERT_TRUE(strstr(r.err, "sync") != NULL || strstr(r.err, "iCloud") != NULL ||
                        strstr(r.err, "CloudDocs") != NULL || strstr(r.err, "Dropbox") != NULL);
        }
        cmd_result_free(&r);
        free(path);
    }
    free(syncish);
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
    r = run_remember(db, a, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    /* key: null for keyless */
    ASSERT_STR_CONTAINS(g.out, "\"key\":null");
    cmd_result_free(&g);
    free(db);
}

void register_schema_config_tests(void)
{
    RUN_TEST(sync_path_warning_on_clouddocs_marker);
    RUN_TEST(json_entry_includes_key_field_null_when_keyless);
}
