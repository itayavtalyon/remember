#include "harness.h"
#include "register.h"
#include "test.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* mkdtemp: Darwin in unistd.h; glibc via stdlib + _POSIX_C_SOURCE. */
#ifdef __APPLE__
#include <unistd.h>
#endif

TEST(util_path_looks_synced_markers)
{
    ASSERT_EQ_INT(util_path_looks_synced(NULL), 0);
    ASSERT_EQ_INT(util_path_looks_synced(""), 0);
    ASSERT_EQ_INT(util_path_looks_synced("/tmp/local/remember.db"), 0);
    ASSERT_EQ_INT(util_path_looks_synced("/Users/me/.remember/remember.db"), 0);

    ASSERT_EQ_INT(util_path_looks_synced("/Users/me/Library/Mobile Documents/"
                                         "com~apple~CloudDocs/Code/db.db"),
                  1);
    ASSERT_EQ_INT(util_path_looks_synced("/tmp/com~apple~CloudDocs-x/t.db"), 1);
    ASSERT_EQ_INT(util_path_looks_synced("/Users/me/Dropbox/notes/remember.db"), 1);
    ASSERT_EQ_INT(util_path_looks_synced("/tmp/Dropbox/t.db"), 1);
    ASSERT_EQ_INT(util_path_looks_synced("/Users/me/Google Drive/remember.db"), 1);
    ASSERT_EQ_INT(util_path_looks_synced("/tmp/Google Drive/t.db"), 1);
}

/* Black-box: each design marker must produce a stderr warning and still succeed. */
static void assert_sync_warning_for_marker(const char *template_dir)
{
    char *syncish = NULL;
    char *path = NULL;
    CmdResult r;
    const char *a[] = {"add", "sync path note"};
    size_t n;
    size_t pn;

    n = strlen(template_dir) + 1U;
    syncish = malloc(n);
    ASSERT_TRUE(syncish != NULL);
    if (syncish == NULL) {
        return;
    }
    (void)snprintf(syncish, n, "%s", template_dir);
    if (mkdtemp(syncish) == NULL) {
        free(syncish);
        ASSERT_TRUE(0);
        return;
    }

    pn = strlen(syncish) + strlen("/t.db") + 1U;
    path = malloc(pn);
    ASSERT_TRUE(path != NULL);
    if (path == NULL) {
        (void)remove(syncish);
        free(syncish);
        return;
    }
    (void)snprintf(path, pn, "%s/t.db", syncish);

    r = run_remember(path, a, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(r.err != NULL && r.err[0] != '\0');
    if (r.err != NULL) {
        ASSERT_TRUE(strstr(r.err, "sync") != NULL || strstr(r.err, "iCloud") != NULL ||
                    strstr(r.err, "CloudDocs") != NULL || strstr(r.err, "Dropbox") != NULL ||
                    strstr(r.err, "Google Drive") != NULL);
    }
    cmd_result_free(&r);
    (void)remove(path);
    free(path);
    (void)remove(syncish);
    free(syncish);
}

TEST(sync_path_warning_on_clouddocs_marker)
{
    assert_sync_warning_for_marker("/tmp/com~apple~CloudDocs-remember-XXXXXX");
}

TEST(sync_path_warning_on_dropbox_marker)
{
    assert_sync_warning_for_marker("/tmp/Dropbox-remember-XXXXXX");
}

TEST(sync_path_warning_on_google_drive_marker)
{
    /* Space in the template so the path embeds the exact "Google Drive" marker. */
    assert_sync_warning_for_marker("/tmp/Google Drive-remember-XXXXXX");
}

void register_schema_config_tests(void)
{
    RUN_TEST(util_path_looks_synced_markers);
    RUN_TEST(sync_path_warning_on_clouddocs_marker);
    RUN_TEST(sync_path_warning_on_dropbox_marker);
    RUN_TEST(sync_path_warning_on_google_drive_marker);
}
