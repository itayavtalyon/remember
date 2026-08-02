#include "remember_app.h"

#include "appio.h"
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

static void print_version(void)
{
    (void)fprintf(app_out(), "remember %s\n", REMEMBER_VERSION);
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
                               "  tags      List all tags with entry counts\n"
                               "  help      Show this help (help <command> for a command)\n"
                               "  version   Show version\n"
                               "\n"
                               "Global options (allowed before or after the command):\n"
                               "  --db PATH   Database file (overrides REMEMBER_DB)\n"
                               "  --json      Machine-readable JSON on stdout\n"
                               "  --help, -h  Show help\n"
                               "  --version   Show version\n"
                               "\n"
                               "Exit codes:\n"
                               "  0  success (including empty search/list)\n"
                               "  1  usage or error\n"
                               "  2  not found (get/delete/update)\n";

    (void)fputs(help, app_out());
}

static void print_command_help(CliCommand topic)
{
    FILE *out = app_out();
    const char *name = cli_command_name(topic);
    const char *summary = cli_command_summary(topic);

    if (name == NULL) {
        print_general_help();
        return;
    }
    (void)fprintf(out, "remember %s - %s\n", name, summary != NULL ? summary : "");
    (void)fprintf(out, "\n");
    (void)fprintf(out, "Usage:\n");
    (void)fprintf(out, "  remember [global options] %s [command options] [args]\n", name);
    (void)fprintf(out, "\n");
    if (topic == CLI_CMD_ADD) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --key KEY       Upsert into a named slot\n");
        (void)fprintf(out, "  --tag TAG       Attach a tag (repeatable; union on merge)\n");
        (void)fprintf(out, "  --source SRC    human|agent|tool|unknown (default unknown)\n");
        (void)fprintf(out, "  BODY|-          Memory text, or - to read stdin\n");
        (void)fprintf(out, "  -- -            Literal body \"-\" (end of options; not stdin)\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_GET || topic == CLI_CMD_DELETE) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_UPDATE) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        (void)fprintf(out, "  --text BODY|-   New body text, or - for stdin\n");
        (void)fprintf(out, "  --text=-        Literal body \"-\" (not stdin)\n");
        (void)fprintf(out, "  --tag TAG       Replace tag set (repeatable)\n");
        (void)fprintf(out, "  --clear-tags    Clear all tags\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required.\n");
        (void)fprintf(out, "At least one of --text, --tag, or --clear-tags is required.\n");
        (void)fprintf(out, "Cannot combine --tag with --clear-tags.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_LIST) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --tag TAG       Require tag (repeatable; AND)\n");
        (void)fprintf(out, "  --source SRC    Filter by source\n");
        (void)fprintf(out, "  --key KEY       Exact key match\n");
        (void)fprintf(out, "  --limit N       Page size (default 20, max 1000)\n");
        (void)fprintf(out, "  --offset M      Skip M matches (default 0)\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_SEARCH) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  QUERY           FTS5 full-text query (required)\n");
        (void)fprintf(out, "  --tag TAG       Require tag (repeatable; AND)\n");
        (void)fprintf(out, "  --source SRC    Filter by source\n");
        (void)fprintf(out, "  --key KEY       Exact key match\n");
        (void)fprintf(out, "  --limit N       Page size (default 20, max 1000)\n");
        (void)fprintf(out, "  --offset M      Skip M matches (default 0)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Ranked by FTS relevance (bm25), then updated_at.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_TAGS) {
        (void)fprintf(out, "Takes no options. Lists every tag with its entry count,\n");
        (void)fprintf(out, "sorted by name. Human: one \"name<TAB>count\" line per tag.\n");
        (void)fprintf(out, "\n");
    }
    (void)fprintf(out, "Global options: --db PATH, --json, --help, --version\n");
    (void)fprintf(out, "See also: remember --help\n");
}

static void print_parse_error(const CliArgs *args)
{
    FILE *err = app_err();
    const char *base = cli_error_message(args->error);

    switch (args->error) {
    case CLI_ERR_UNKNOWN_COMMAND:
    case CLI_ERR_UNKNOWN_OPTION:
        if (args->error_arg != NULL) {
            (void)fprintf(err, "remember: %s '%s'\n", base, args->error_arg);
            return;
        }
        break;
    case CLI_ERR_MISSING_OPTION_VALUE:
        if (args->error_option != NULL) {
            (void)fprintf(err, "remember: %s: %s\n", args->error_option, base);
            return;
        }
        break;
    case CLI_ERR_OK:
    case CLI_ERR_MISSING_COMMAND:
    case CLI_ERR_INTERNAL:
    default:
        break;
    }
    (void)fprintf(err, "remember: %s\n", base);
}

/* Resolve path, optional sync-path warning, then open. Store stays path-pure. */
static Store *open_store(const CliArgs *args, int *out_rc)
{
    char path[REMEMBER_PATH_MAX];
    char err[256];
    Store *s;

    err[0] = '\0';
    if (util_resolve_db_path(args->globals.db_path, path, sizeof(path), err, sizeof(err)) != 0) {
        (void)fprintf(app_err(), "remember: %s\n", err[0] != '\0' ? err : "invalid database path");
        *out_rc = REMEMBER_ERR;
        return NULL;
    }
    /* Warn only: synced volumes can corrupt any journal mode; still open. */
    if (util_path_looks_synced(path)) {
        (void)fprintf(app_err(),
                      "remember: warning: database path looks like a synced folder "
                      "(iCloud/Dropbox/Google Drive); corruption risk - prefer a local disk\n");
    }
    s = store_open(path, err, sizeof(err));
    if (s == NULL) {
        (void)fprintf(app_err(), "remember: %s\n", err[0] != '\0' ? err : "cannot open database");
        *out_rc = REMEMBER_ERR;
        return NULL;
    }
    return s;
}

typedef int (*CmdFn)(Store *s, bool json, int rest_argc, const char **rest_argv);

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
        return run_with_store(args, cmd_search);
    case CLI_CMD_UPDATE:
        return run_with_store(args, cmd_update);
    case CLI_CMD_TAGS:
        return run_with_store(args, cmd_tags);
    case CLI_CMD_NONE:
        /* Defensive: parse should set CLI_ERR_MISSING_COMMAND first. */
        (void)fprintf(app_err(), "remember: %s\n", cli_error_message(CLI_ERR_MISSING_COMMAND));
        return REMEMBER_ERR;
    case CLI_CMD_UNKNOWN:
        /* Defensive: parse should set CLI_ERR_UNKNOWN_COMMAND first. */
        (void)fprintf(app_err(), "remember: %s\n", cli_error_message(CLI_ERR_UNKNOWN_COMMAND));
        return REMEMBER_ERR;
    default:
        (void)fprintf(app_err(), "remember: internal error\n");
        return REMEMBER_ERR;
    }
}

int remember_run(int argc, char *const *argv, FILE *out, FILE *err)
{
    CliArgs args;
    int rc;

    app_set_streams(out, err);
    cli_parse(argc, argv, &args);
    rc = run(&args);
    cli_args_free(&args);
    app_set_streams(NULL, NULL);
    return rc;
}

int remember_main(int argc, char *const *argv)
{
    return remember_run(argc, argv, stdout, stderr);
}
