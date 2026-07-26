#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
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
    {"update", register_update_tests},
    {"json_db_config", register_json_db_config_tests},
    {"key_gld", register_key_gld_tests},
    {"key", register_key_tests},
    {"schema_config", register_schema_config_tests},
    {"verification_gld", register_verification_gld_tests},
    {"verification_edges", register_verification_edges_tests},
    {"coverage_edges", register_coverage_edges_tests},
};

/* Exact comma-token match: "key" must not select "key_gld" (or vice versa),
   which a plain substring test would wrongly do and quietly corrupt the gate. */
static int group_selected(const char *only, const char *name)
{
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

/*
 * usage: remember_tests <remember-binary> [--only GROUP[,GROUP...]]
 * --only runs a subset (the implemented gate); without it, the whole suite runs
 * (expected-fail during incremental TDD). Exit 0 only if every run test passed.
 */
int main(int argc, char **argv)
{
    const char *only = NULL;
    size_t i;
    int arg;

    if (argc < 2) {
        (void)fprintf(stderr, "usage: %s <path-to-remember-binary> [--only GROUP,...]\n", argv[0]);
        return 2;
    }
    g_remember_bin = argv[1];

    for (arg = 2; arg < argc; arg++) {
        if (strcmp(argv[arg], "--only") == 0 && arg + 1 < argc) {
            only = argv[arg + 1];
            arg++;
        }
    }

    for (i = 0; i < sizeof(k_groups) / sizeof(k_groups[0]); i++) {
        if (only == NULL || group_selected(only, k_groups[i].name)) {
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
