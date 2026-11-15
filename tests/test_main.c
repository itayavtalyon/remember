#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    void (*run)(void);
} TestGroup;

static const TestGroup k_groups[] = {
    {"cli_global", register_cli_global_tests},
    {"store", register_store_tests},
    {"normalize", register_normalize_tests},
    {"add", register_add_tests},
    {"get_list_delete", register_get_list_delete_tests},
    {"search", register_search_tests},
    {"union_search", register_union_search_tests},
    {"update", register_update_tests},
    {"json_db_config", register_json_db_config_tests},
    {"key_gld", register_key_gld_tests},
    {"key", register_key_tests},
    {"schema_config", register_schema_config_tests},
    {"verification_gld", register_verification_gld_tests},
    {"verification_search", register_verification_search_tests},
    {"verification_edges", register_verification_edges_tests},
    {"coverage_edges", register_coverage_edges_tests},
    {"facade", register_facade_tests},
    {"tags", register_tags_tests},
    {"ttl", register_ttl_tests},
    {"link", register_link_tests},
    {"sync_output", register_sync_output_tests},
    {"sync_delete", register_sync_delete_tests},
    {"sync_locators", register_sync_locators_tests},
    {"sync_import", register_sync_import_tests},
    {"schema_v5", register_schema_v5_tests},
    {"schema_v6", register_schema_v6_tests},
    {"schema_v6_cli", register_schema_v6_cli_tests},
    {"device", register_device_tests},
    {"errors", register_errors_tests},
    {"list_dates", register_list_dates_tests},
    {"settings", register_settings_tests},
    {"envelope", register_envelope_tests},
    {"export", register_export_tests},
    {"import_formats", register_import_formats_tests},
    {"outbox", register_outbox_tests},
    {"old_cli", register_old_cli_tests},
    {"plugins", register_plugins_tests},
    {"brain_list", register_brain_list_tests},
};

/* Exact comma-token match: "key" must not select "key_gld" (or vice versa),
   which a plain substring test would wrongly do and quietly corrupt the gate. */
typedef struct {
    const char *only;
    const char *name;
} GroupSel;

static int group_selected(GroupSel sel)
{
    const char *only = sel.only;
    const char *name = sel.name;
    size_t namelen = strlen(name);
    const char *p = only;

    while (*p != '\0') {
        const char *comma = strchr(p, ',');
        size_t toklen = (comma != NULL) ? (size_t)(comma - p) : strlen(p);
        if (toklen == namelen && strncmp(p, name, namelen) == 0) {
            return 1;
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return 0;
}

/* Every read-write open writes <data dir>/brains.json (plan 26), so no test may reach the
   real data dir, and one test's brains must not be listed in the next test's (every brain
   made under REMEMBER_TEST_FIXED_IDS has the same id, which reads as a copy). Each test
   starts with XDG_DATA_HOME in its own temp folder. A test that needs another value sets
   it and puts this one back. */
static void isolate_data_dir(void)
{
    char *db = make_temp_db_path();
    char *dir = db != NULL ? dir_of_path(db) : NULL;

    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
    if (dir == NULL || setenv("XDG_DATA_HOME", dir, 1) != 0) {
        (void)fprintf(stderr, "cannot isolate the data dir\n");
        exit(2); // NOLINT(concurrency-mt-unsafe) single-threaded test process
    }
    free(dir);
    free(db);
}

/*
 * usage: remember_tests <remember-binary> [--only GROUP[,GROUP...]]
 * --only runs a subset (the implemented gate); without it, the whole suite runs
 * (expected-fail during incremental TDD). Exit 0 only if every run test passed.
 */
int main(int argc, char **argv)
{
    const char *only = NULL;
    size_t i = 0;
    int arg = 0;

    if (argc < 2) {
        (void)fprintf(stderr, "usage: %s <path-to-remember-binary> [--only GROUP,...]\n", argv[0]);
        return 2;
    }
    g_remember_bin = argv[1];
    tst_set_setup(isolate_data_dir);

    for (arg = 2; arg < argc; arg++) {
        if (strcmp(argv[arg], "--only") == 0 && arg + 1 < argc) {
            only = argv[arg + 1];
            arg++;
        }
    }

    for (i = 0; i < sizeof(k_groups) / sizeof(k_groups[0]); i++) {
        if (only == NULL || group_selected((GroupSel){.only = only, .name = k_groups[i].name})) {
            k_groups[i].run();
        }
    }

    (void)printf("\n== summary ==\n");
    (void)printf("tests run:     %d\n", g_tests_run);
    (void)printf("tests failed:  %d\n", g_tests_failed);
    (void)printf("asserts run:   %d\n", g_asserts_run);
    (void)printf("asserts fail:  %d\n", g_asserts_failed);

    if (g_tests_run == 0) {
        (void)fprintf(stderr, "no tests matched --only filter\n");
        return 2;
    }
    return (g_asserts_failed == 0) ? 0 : 1;
}
