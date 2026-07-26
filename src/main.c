#include "cli.h"
#include "commands.h"
#include "exit_codes.h"
#include "store.h"
#include "util.h"

#include <stdbool.h>
#include <stdio.h>

#ifndef REMEMBER_VERSION
#define REMEMBER_VERSION "0.1.0"
#endif

/* Scaffold only — not part of the stable exit-code contract (0/1/2). */
#define REMEMBER_NYI 3

static void print_version(void)
{
    (void)printf("remember %s\n", REMEMBER_VERSION);
}

static void print_general_help(void)
{
    static const char help[] = "remember - local personal memory (SQLite + FTS5)\n"
                               "\n"
                               "Usage:\n"
                               "  remember [--db PATH] [--json] <command> [options] [args]\n"
                               "\n"
                               "Commands:\n"
                               "  add       Store a memory (optional --key, --tag, --source)\n"
                               "  search    Full-text search\n"
                               "  list      List memories with filters\n"
                               "  get       Fetch one entry by id or --key\n"
                               "  update    Change body and/or tags by id or --key\n"
                               "  delete    Remove an entry by id or --key\n"
                               "  help      Show this help (help <command> for a command)\n"
                               "  version   Show version\n"
                               "\n"
                               "Global options (allowed before or after the command):\n"
                               "  --db PATH   Database file (overrides REMEMBER_DB)\n"
                               "  --json      Machine-readable JSON on stdout\n"
                               "  --help, -h  Show help\n"
                               "  --version   Show version\n";

    (void)fputs(help, stdout);
}

static void print_command_help(CliCommand topic)
{
    const char *name = cli_command_name(topic);
    const char *summary = cli_command_summary(topic);

    if (name == NULL) {
        print_general_help();
        return;
    }
    (void)printf("remember %s - %s\n", name, summary != NULL ? summary : "");
    (void)printf("\n");
    (void)printf("Usage:\n");
    (void)printf("  remember [global options] %s [command options] [args]\n", name);
    (void)printf("\n");
    if (topic == CLI_CMD_ADD) {
        (void)printf("Options:\n");
        (void)printf("  --key KEY       Upsert into a named slot\n");
        (void)printf("  --tag TAG       Attach a tag (repeatable; union on merge)\n");
        (void)printf("  --source SRC    human|agent|tool|unknown (default unknown)\n");
        (void)printf("  BODY|-          Memory text, or - to read stdin\n");
        (void)printf("\n");
    } else if (topic == CLI_CMD_GET || topic == CLI_CMD_DELETE) {
        (void)printf("Options:\n");
        (void)printf("  ID              Entry id (positional)\n");
        (void)printf("  --key KEY       Locate by key instead of id\n");
        (void)printf("\n");
        (void)printf("Exactly one of ID or --key is required.\n");
        (void)printf("\n");
    } else if (topic == CLI_CMD_LIST) {
        (void)printf("Options:\n");
        (void)printf("  --tag TAG       Require tag (repeatable; AND)\n");
        (void)printf("  --source SRC    Filter by source\n");
        (void)printf("  --key KEY       Exact key match\n");
        (void)printf("  --limit N       Page size (default 20, max 1000)\n");
        (void)printf("  --offset M      Skip M matches (default 0)\n");
        (void)printf("\n");
    }
    (void)printf("Global options: --db PATH, --json, --help, --version\n");
    (void)printf("See also: remember --help\n");
}

static void print_parse_error(const CliArgs *args)
{
    const char *base = cli_error_message(args->error);

    switch (args->error) {
    case CLI_ERR_UNKNOWN_COMMAND:
    case CLI_ERR_UNKNOWN_OPTION:
        if (args->error_arg != NULL) {
            (void)fprintf(stderr, "remember: %s '%s'\n", base, args->error_arg);
            return;
        }
        break;
    case CLI_ERR_MISSING_OPTION_VALUE:
        if (args->error_option != NULL) {
            (void)fprintf(stderr, "remember: %s: %s\n", args->error_option, base);
            return;
        }
        break;
    case CLI_ERR_OK:
    case CLI_ERR_MISSING_COMMAND:
    case CLI_ERR_INTERNAL:
    default:
        break;
    }
    (void)fprintf(stderr, "remember: %s\n", base);
}

static int dispatch_nyi(const char *name)
{
    (void)fprintf(stderr, "remember: command '%s' is not implemented yet\n", name);
    return REMEMBER_NYI;
}

/* Resolve --db / env / default and open. On failure prints and sets *out_rc. */
static Store *open_store(const CliArgs *args, int *out_rc)
{
    char path[REMEMBER_PATH_MAX];
    char err[256];
    Store *s;

    err[0] = '\0';
    if (util_resolve_db_path(args->globals.db_path, path, sizeof(path), err, sizeof(err)) != 0) {
        (void)fprintf(stderr, "remember: %s\n", err[0] != '\0' ? err : "invalid database path");
        *out_rc = REMEMBER_ERR;
        return NULL;
    }
    s = store_open(path, err, sizeof(err));
    if (s == NULL) {
        (void)fprintf(stderr, "remember: %s\n", err[0] != '\0' ? err : "cannot open database");
        *out_rc = REMEMBER_ERR;
        return NULL;
    }
    return s;
}

typedef int (*CmdFn)(Store *s, bool json, int rest_argc, const char **rest_argv);

/* Open store, run a command, close. Propagates open or command exit codes. */
static int run_with_store(const CliArgs *args, CmdFn fn)
{
    Store *s;
    int rc;

    s = open_store(args, &rc);
    if (s == NULL) {
        return rc;
    }
    rc = fn(s, args->globals.json, args->rest_argc, args->rest_argv);
    store_close(s);
    return rc;
}

static int run(const CliArgs *args)
{
    if (args->error != CLI_ERR_OK) {
        print_parse_error(args);
        return REMEMBER_ERR;
    }

    switch (args->command) {
    case CLI_CMD_HELP:
        if (args->help_topic != CLI_CMD_NONE) {
            print_command_help(args->help_topic);
        } else {
            print_general_help();
        }
        return REMEMBER_OK;
    case CLI_CMD_VERSION:
        print_version();
        return REMEMBER_OK;
    case CLI_CMD_ADD:
        return run_with_store(args, cmd_add);
    case CLI_CMD_GET:
        return run_with_store(args, cmd_get);
    case CLI_CMD_LIST:
        return run_with_store(args, cmd_list);
    case CLI_CMD_DELETE:
        return run_with_store(args, cmd_delete);
    case CLI_CMD_SEARCH:
        return dispatch_nyi("search");
    case CLI_CMD_UPDATE:
        return dispatch_nyi("update");
    case CLI_CMD_NONE:
        /* Usually set with CLI_ERR_MISSING_COMMAND (handled above); keep distinct. */
        (void)fprintf(stderr, "remember: %s\n", cli_error_message(CLI_ERR_MISSING_COMMAND));
        return REMEMBER_ERR;
    case CLI_CMD_UNKNOWN:
        /* Usually set with CLI_ERR_UNKNOWN_COMMAND (handled above); keep distinct. */
        (void)fprintf(stderr, "remember: %s\n", cli_error_message(CLI_ERR_UNKNOWN_COMMAND));
        return REMEMBER_ERR;
    default:
        (void)fprintf(stderr, "remember: internal error\n");
        return REMEMBER_ERR;
    }
}

int main(int argc, char **argv)
{
    CliArgs args;
    int rc;

    cli_parse(argc, argv, &args);
    rc = run(&args);
    cli_args_free(&args);
    return rc;
}
