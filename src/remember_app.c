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
#define REMEMBER_VERSION "0.1.1"
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
                               "  tags        List all tags with entry counts\n"
                               "  purge       Permanently delete one bin (--expired or --deleted)\n"
                               "  purge-trash Permanently delete every expired memory\n"
                               "  link        Create or merge an entry link\n"
                               "  unlink      Remove entry links\n"
                               "  related     List neighbors of an entry\n"
                               "  rekey       Rename, set, or clear an entry key in place\n"
                               "  import      Merge another remember database (--from-db)\n"
                               "  conflicts   List unresolved merge conflicts\n"
                               "  conflict    Resolve a merge conflict (accept)\n"
                               "  help        Show this help (help <command> for a command)\n"
                               "  version     Show version\n"
                               "\n"
                               "Expiry is optional. Default list/search/get hide trash.\n"
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
                               "  2  not found (get/delete/update)\n"
                               "  3  wrong bin (expired / not_expired)\n";

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
        (void)fprintf(out, "  --source SRC    human|agent|tool|share|unknown (default unknown)\n");
        (void)fprintf(out, "  --ttl 7d        Relative expiry from write time (Nm/Nh/Nd/Nw)\n");
        (void)fprintf(out, "  --expires TS    Absolute expiry (YYYY-MM-DD local EOD, or ...Z)\n");
        (void)fprintf(out, "  BODY|-          Memory text, or - to read stdin\n");
        (void)fprintf(out, "  -- -            Literal body \"-\" (end of options; not stdin)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Cannot combine --ttl with --expires.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_GET || topic == CLI_CMD_DELETE) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        (void)fprintf(out, "  --trash         Read/delete from trash only\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required.\n");
        (void)fprintf(out, "Default locators are active-only; --trash is trash-only.\n");
        if (topic == CLI_CMD_GET) {
            (void)fprintf(out, "JSON entries include links stubs after expires_at.\n");
            (void)fprintf(out, "Human get prints the body, then a Related: block if any.\n");
        }
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_UPDATE) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        (void)fprintf(out, "  --trash         Locate in trash (required to restore)\n");
        (void)fprintf(out, "  --text BODY|-   New body text, or - for stdin\n");
        (void)fprintf(out, "  --text=-        Literal body \"-\" (not stdin)\n");
        (void)fprintf(out, "  --tag TAG       Replace tag set (repeatable)\n");
        (void)fprintf(out, "  --clear-tags    Clear all tags\n");
        (void)fprintf(out, "  --ttl 7d        Set relative expiry from this update\n");
        (void)fprintf(out, "  --expires TS    Set absolute expiry\n");
        (void)fprintf(out, "  --clear-expires Remove expiry (restore when used with --trash)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required.\n");
        (void)fprintf(out, "At least one of --text, --tag, --clear-tags, --ttl, --expires,\n");
        (void)fprintf(out, "or --clear-expires is required.\n");
        (void)fprintf(out, "Cannot combine --tag with --clear-tags.\n");
        (void)fprintf(out, "Cannot combine --clear-expires with --ttl or --expires.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_LIST) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --tag TAG       Require tag (repeatable; AND)\n");
        (void)fprintf(out, "  --source SRC    Filter by source\n");
        (void)fprintf(out, "  --key KEY       Exact key match\n");
        (void)fprintf(out, "  --limit N       Page size (default 20, max 1000)\n");
        (void)fprintf(out, "  --offset M      Skip M matches (default 0)\n");
        (void)fprintf(out, "  --trash         List trash only (default: active only)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Human: id | key | tags | preview | updated_at | related\n");
        (void)fprintf(out, "related cell: neighbor ids (cap 5, then , +N). JSON links stubs.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_SEARCH) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  QUERY           FTS5 full-text query (required)\n");
        (void)fprintf(out, "  --tag TAG       Require tag (repeatable; AND)\n");
        (void)fprintf(out, "  --source SRC    Filter by source\n");
        (void)fprintf(out, "  --key KEY       Exact key match\n");
        (void)fprintf(out, "  --limit N       Page size (default 20, max 1000)\n");
        (void)fprintf(out, "  --offset M      Skip M matches (default 0)\n");
        (void)fprintf(out, "  --trash         Search trash only (default: active only)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Ranked by FTS relevance (bm25), then updated_at.\n");
        (void)fprintf(out, "Human columns match list (sixth is related ids). JSON links stubs.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_TAGS) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --trash         Count tags among trash only (default: active)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Lists every in-use tag with its entry count, sorted by name.\n");
        (void)fprintf(out, "Human: one \"name<TAB>count\" line per tag.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_PURGE) {
        (void)fprintf(out, "Permanently delete every row in one bin (no prompt).\n");
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --expired       Wipe expired rows\n");
        (void)fprintf(out, "  --deleted       Wipe deleted rows\n");
        (void)fprintf(out, "Exactly one of --expired or --deleted is required.\n");
        (void)fprintf(out, "Human stdout: the count of deleted entries.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_PURGE_TRASH) {
        (void)fprintf(out, "Permanently delete every expired row (no prompt).\n");
        (void)fprintf(out, "Human stdout: the count of deleted entries.\n");
        (void)fprintf(out, "Takes no options.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_LINK || topic == CLI_CMD_UNLINK) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --from ID       Source entry id\n");
        (void)fprintf(out, "  --from-key KEY  Source entry key\n");
        (void)fprintf(out, "  --to ID         Target entry id\n");
        (void)fprintf(out, "  --to-key KEY    Target entry key\n");
        (void)fprintf(out, "  --kind KIND     related (default on link) | supersedes | cites\n");
        (void)fprintf(out, "  ID ID           Sugar for --from / --to numeric ids\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out,
                      "Each end is exactly one of id or --key. Locators resolve in either bin.\n");
        if (topic == CLI_CMD_UNLINK) {
            (void)fprintf(out, "Omit --kind to remove every kind between the pair.\n");
        }
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_RELATED) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        (void)fprintf(out, "  --kind KIND     Filter stored kind (related|supersedes|cites)\n");
        (void)fprintf(out, "  --outgoing      Directed from this entry; related still included\n");
        (void)fprintf(out, "  --incoming      Directed to this entry; related still included\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key. Either bin (no --trash).\n");
        (void)fprintf(out, "Cannot combine --outgoing and --incoming.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_REKEY) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        (void)fprintf(out, "  --trash         Locate in trash (same bin contract as update)\n");
        (void)fprintf(out, "  --to-key NEW    Set or rename the key (non-empty)\n");
        (void)fprintf(out, "  --clear-key     Remove the key (keyed becomes keyless)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out,
                      "Exactly one of ID or --key, and exactly one of --to-key or --clear-key.\n");
        (void)fprintf(out, "Empty --to-key is illegal (not a clear).\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_IMPORT) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --from-db PATH  Source remember database (read-only)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Merges by sync_id. Conflicts are recorded; exit 0 even if any.\n");
        (void)fprintf(out, "Run 'conflicts' after import. Source must be schema v4+.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_CONFLICTS) {
        (void)fprintf(out, "Lists unresolved import conflicts. Takes no options.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_CONFLICT) {
        (void)fprintf(out, "Usage:\n");
        (void)fprintf(out, "  remember conflict accept --id N --keep local|incoming|both\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out,
                      "both remints sync_id on concurrent_vv; clash keeps incoming sync_id.\n");
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
    enum { ERR_BUFSIZE = 256 }; /* store_open error-message buffer */
    char path[REMEMBER_PATH_MAX];
    char err[ERR_BUFSIZE];
    Store *s = NULL;

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
    Store *s = NULL;
    int rc = 0;

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
    case CLI_CMD_PURGE_TRASH:
        return run_with_store(args, cmd_purge_trash);
    case CLI_CMD_PURGE:
        return run_with_store(args, cmd_purge);
    case CLI_CMD_LINK:
        return run_with_store(args, cmd_link);
    case CLI_CMD_UNLINK:
        return run_with_store(args, cmd_unlink);
    case CLI_CMD_RELATED:
        return run_with_store(args, cmd_related);
    case CLI_CMD_REKEY:
        return run_with_store(args, cmd_rekey);
    case CLI_CMD_IMPORT:
        return run_with_store(args, cmd_import);
    case CLI_CMD_CONFLICTS:
        return run_with_store(args, cmd_conflicts);
    case CLI_CMD_CONFLICT:
        return run_with_store(args, cmd_conflict);
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

/* cppcheck-suppress staticFunction ; public entry point (remember_app.h); used by main + GUI +
 * tests */
int remember_run(int argc, char *const *argv, FILE *out, FILE *err)
{
    CliArgs args;
    int rc = 0;

    app_set_streams((AppStreams){.out = out, .err = err});
    cli_parse(argc, argv, &args);
    rc = run(&args);
    cli_args_free(&args);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    return rc;
}

int remember_main(int argc, char *const *argv)
{
    return remember_run(argc, argv, stdout, stderr);
}
