#include "test.h"
#include "harness.h"
#include "register.h"

#include <stdio.h>
#include <stdlib.h>

int g_tests_run;
int g_tests_failed;
int g_asserts_run;
int g_asserts_failed;
const char *g_current_test;

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-remember-binary>\n", argv[0]);
        return 2;
    }
    g_remember_bin = argv[1];

    g_tests_run = 0;
    g_tests_failed = 0;
    g_asserts_run = 0;
    g_asserts_failed = 0;

    register_cli_global_tests();
    register_add_tests();
    register_get_list_delete_tests();
    register_search_tests();
    register_update_tests();
    register_json_db_config_tests();
    register_key_tests();
    register_schema_config_tests();
    register_verification_edges_tests();

    printf("\n== summary ==\n");
    printf("tests run:     %d\n", g_tests_run);
    printf("tests failed:  %d\n", g_tests_failed);
    printf("asserts run:   %d\n", g_asserts_run);
    printf("asserts fail:  %d\n", g_asserts_failed);

    if (g_asserts_failed == 0) {
        printf("ALL PASSED (unexpected at skeleton stage)\n");
        return 0;
    }
    printf("FAILURES (expected until implementation is complete)\n");
    return 1;
}
