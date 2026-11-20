#include "remember_app.h"

#include "appio.h"
#include "brain_note.h"
#include "cli.h"
#include "commands.h"
#include "commands_common.h"
#include "diag.h"
#include "events.h"
#include "exit_codes.h"
#include "output.h"
#include "plugin_settings.h"
#include "settings.h"
#include "store.h"
#include "sweep.h"
#include "util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The envelope when --json cannot get the memory to build one: type, then fix. */
static const char k_out_of_memory_reply[] =
    "{\"format\":\"remember-message\",\"version\":1,\"type\":\"%s.result\",\"error\":{"
    "\"code\":\"out_of_memory\",\"message\":\"out of "
    "memory\",\"fix\":\"%s\",\"retryable\":true}}\n";

static void print_version(void)
{
    (void)fprintf(app_out(), "remember %s\n", REMEMBER_VERSION);
}

static void print_general_help(void)
{
    static const char help[] =
        "remember - local personal memory (SQLite + FTS5)\n"
        "\n"
        "Usage:\n"
        "  remember [--db PATH] [--json] [--strict] "
        "[--color=auto|always|never] [--quiet] "
        "[--busy-timeout MS] [--no-create] <command> [options] [args]\n"
        "\n"
        "Commands:\n"
        "  add       Store a memory (optional --key, --tag, --as)\n"
        "  search    Full-text search\n"
        "  repair    Rebuild the search index\n"
        "  list      List memories with filters\n"
        "  get       Fetch one entry by id or --key\n"
        "  update    Change body and/or tags by id or --key\n"
        "  delete    Move to the Trash (--expired: every expired memory)\n"
        "  tags        List all tags with entry counts\n"
        "  purge       Permanently delete one expired or deleted memory\n"
        "  link        Create or merge an entry link\n"
        "  unlink      Remove entry links\n"
        "  related     List neighbors of an entry\n"
        "  rekey       Rename, set, or clear an entry key in place\n"
        "  import      Merge a remember export or database\n"
        "  conflicts   List unresolved merge conflicts\n"
        "  conflict    Resolve a merge conflict (accept)\n"
        "  device      Print the device id, or mint one with 'new'\n"
        "  brain       Replace the brain id\n"
        "  export      Write the brain to a JSON, Markdown, or CSV file\n"
        "  settings    Show or change a setting\n"
        "  sweep       Mark memories past their expiry (--all: every listed brain)\n"
        "  help        Show this help (help <command> for a command)\n"
        "  version     Show version\n"
        "\n"
        "Expiry is optional. Default list/search/get show live only.\n"
        "Bins: --expired / --deleted (mutex). --trash selects the Deleted bin.\n"
        "Device id lives next to the DB as <path>.d/device_id.\n"
        "\n"
        "Global options (allowed before or after the command):\n"
        "  --db PATH          Database file (overrides REMEMBER_DB)\n"
        "  --json             Machine-readable JSON on stdout\n"
        "  --color     Use color: auto, always, or never. auto uses color only on a terminal, "
        "when NO_COLOR is unset and TERM isn't dumb.\n"
        "  --quiet     Turn off progress lines on stderr.\n"
        "  --busy-timeout MS  Lock wait in ms (0..60000, default 5000)\n"
        "  --no-create        Do not create a missing brain file\n"
        "  --help, -h         Show help\n"
        "  --version          Show version\n"
        "\n"
        "Exit codes:\n"
        "  0  success (including empty search/list)\n"
        "  1  usage or error\n"
        "  2  not found (get/delete/update)\n"
        "  3  wrong bin (expired / not_expired / deleted / not_deleted)\n"
        "  4  a warning under --strict\n"
        "  5  confirmation or approval required\n"
        "  75 brain is read-only (made by a newer version of remember)\n";

    (void)fputs(help, app_out());
}

static void print_trash_expired_help(FILE *out)
{
    (void)fprintf(out, "  --trash     Use the Trash: memories you deleted. Restore them, or purge "
                       "them for good.\n");
    (void)fprintf(out, "  --expired   Use expired memories: ones past their expiry time. They are "
                       "not in the Trash.\n");
}

/* H-16.11, locked. Every write accepts both. */
static void print_hooks_help(FILE *out)
{
    (void)fprintf(out, "  --hooks     Run matching hooks for this write, also ones you turned off "
                       "for local writes.\n");
    (void)fprintf(out, "  --no-hooks  Run no hooks for this write.\n");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — one help block per command
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
        (void)fprintf(out, "  --as KIND       human|agent|tool|share (default human)\n");
        (void)fprintf(out, "  --actor ID      1 to 64 of A-Za-z0-9_./:-\n");
        print_hooks_help(out);
        (void)fprintf(out, "  --ttl 7d        Relative expiry from write time (Nm/Nh/Nd/Nw)\n");
        (void)fprintf(out, "  --expires TS    Absolute expiry (YYYY-MM-DD local EOD, or ...Z)\n");
        (void)fprintf(out, "  BODY|-          Memory text, or - to read stdin\n");
        (void)fprintf(out, "  -- -            Literal body \"-\" (end of options; not stdin)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Cannot combine --ttl with --expires.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_GET) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        print_trash_expired_help(out);
        (void)fprintf(out, "  --deleted       Locate in deleted bin\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required.\n");
        (void)fprintf(out, "Default locators are live-only; --expired/--deleted select one bin.\n");
        (void)fprintf(out, "JSON entries include links stubs after version_vector.\n");
        (void)fprintf(out, "Human get prints the body, then a Related: block if any.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_DELETE) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        (void)fprintf(out, "  --expired       Move every expired memory to the Trash (no ID or --key)\n");
        (void)fprintf(out, "  --as KIND       human|agent|tool|share (default human)\n");
        (void)fprintf(out, "  --actor ID      1 to 64 of A-Za-z0-9_./:-\n");
        print_hooks_help(out);
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required, except with --expired.\n");
        (void)fprintf(out, "Delete moves a live or expired memory to the Trash.\n");
        (void)fprintf(out, "To remove one for good, use purge.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_UPDATE) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        print_trash_expired_help(out);
        (void)fprintf(out, "  --deleted       Locate in deleted bin\n");
        (void)fprintf(out, "  --text BODY|-   New body text, or - for stdin\n");
        (void)fprintf(out, "  --text=-        Literal body \"-\" (not stdin)\n");
        (void)fprintf(out, "  --tag TAG       Replace tag set (repeatable)\n");
        (void)fprintf(out, "  --clear-tags    Clear all tags\n");
        (void)fprintf(out, "  --ttl 7d        Set relative expiry from this update\n");
        (void)fprintf(out, "  --expires TS    Set absolute expiry\n");
        (void)fprintf(out, "  --clear-expires Remove expiry\n");
        (void)fprintf(out, "  --undelete      Clear deleted_at\n");
        (void)fprintf(out, "  --if-version-vector VV\n");
        (void)fprintf(out, "                  Write only when version_vector equals VV\n");
        (void)fprintf(out, "  --as KIND       human|agent|tool|share (default human)\n");
        (void)fprintf(out, "  --actor ID      1 to 64 of A-Za-z0-9_./:-\n");
        print_hooks_help(out);
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required.\n");
        (void)fprintf(out, "At least one of --text, --tag, --clear-tags, --ttl, --expires,\n");
        (void)fprintf(out, "--clear-expires, or --undelete is required.\n");
        (void)fprintf(out, "Cannot combine --tag with --clear-tags.\n");
        (void)fprintf(out, "Cannot combine --clear-expires with --ttl or --expires.\n");
        (void)fprintf(out, "A version-vector mismatch is code stale "
                           "(memory changed since it was read) and writes nothing.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_LIST) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --tag TAG       Require tag (repeatable; AND)\n");
        (void)fprintf(out, "  --created-by KIND  human|agent|tool|share|import\n");
        (void)fprintf(out, "  --updated-by KIND  human|agent|tool|share|import\n");
        (void)fprintf(out, "  --key KEY       Exact key match\n");
        (void)fprintf(out, "  --sort SORT     updated or created (default updated)\n");
        (void)fprintf(out, "  --since TS      Inclusive lower bound on the sort field\n");
        (void)fprintf(out, "  --until TS      Exclusive upper bound on the sort field\n");
        (void)fprintf(out, "  --limit N       Page size (default 20, max 1000)\n");
        (void)fprintf(out, "  --cursor CURSOR Resume after the last row\n");
        print_trash_expired_help(out);
        (void)fprintf(out, "  --deleted       List deleted only\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "--since is inclusive and --until is exclusive.\n");
        (void)fprintf(out, "TS is YYYY-MM-DD (local start of that day), YYYY-MM-DDTHH:MM:SSZ,\n");
        (void)fprintf(out, "or YYYY-MM-DDTHH:MM:SS.mmmZ.\n");
        (void)fprintf(out, "Human: id | key | tags | preview | updated_at | related\n");
        (void)fprintf(out, "related cell: neighbor ids (cap 5, then , +N). JSON links stubs.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_SEARCH) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(
            out, "  QUERY           Words or word prefixes; FTS5 syntax also works (required)\n");
        (void)fprintf(out, "  --tag TAG       Require tag (repeatable; AND)\n");
        (void)fprintf(out, "  --created-by KIND  human|agent|tool|share|import\n");
        (void)fprintf(out, "  --updated-by KIND  human|agent|tool|share|import\n");
        (void)fprintf(out, "  --key KEY       Exact key match\n");
        (void)fprintf(out, "  --sort SORT     updated or created; accepted, then ignored\n");
        (void)fprintf(out, "  --since TS      Inclusive lower bound on updated_at\n");
        (void)fprintf(out, "  --until TS      Exclusive upper bound on updated_at\n");
        (void)fprintf(out, "  --limit N       Page size (default 20, max 1000)\n");
        (void)fprintf(out, "  --facet-limit N Keys and tags to return (default 50, max 200)\n");
        print_trash_expired_help(out);
        (void)fprintf(out, "  --deleted       Search deleted only\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Matches word prefixes (boo finds Book) in keys, bodies and tags,\n");
        (void)fprintf(out, "and the query as FTS5 syntax. A query FTS5 cannot parse still\n");
        (void)fprintf(out, "returns the prefix matches, exits 0 and writes\n");
        (void)fprintf(out, "\"remember: warning: raw query ignored: <message>\" on stderr.\n");
        (void)fprintf(out, "An empty or blank query is an empty result.\n");
        (void)fprintf(out, "Put -- before a query that starts with '-'.\n");
        (void)fprintf(out, "Order: exact key, key prefix, relevance (bm25), then updated_at.\n");
        (void)fprintf(out,
                      "JSON adds keys and tags (matches in scope) between total and entries.\n");
        (void)fprintf(out, "Human output prints keys: and tags: lines (at most 10 each) first.\n");
        (void)fprintf(out, "Search ignores --sort. --since and --until filter updated_at.\n");
        (void)fprintf(out, "TS is YYYY-MM-DD (local start of that day), YYYY-MM-DDTHH:MM:SSZ,\n");
        (void)fprintf(out, "or YYYY-MM-DDTHH:MM:SS.mmmZ.\n");
        (void)fprintf(out, "Human columns match list (sixth is related ids). JSON links stubs.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_TAGS) {
        (void)fprintf(out, "Options:\n");
        print_trash_expired_help(out);
        (void)fprintf(out, "  --deleted       Count tags among deleted only\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Lists every in-use tag with its entry count, sorted by name.\n");
        (void)fprintf(out, "Human: one \"name<TAB>count\" line per tag.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_PURGE) {
        (void)fprintf(out, "Permanently delete one memory that has expired or is in the Trash.\n");
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        print_hooks_help(out);
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key is required. There is no prompt.\n");
        (void)fprintf(out, "A live memory is refused (exit 3): delete it first.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_SWEEP) {
        (void)fprintf(out, "Mark every memory past its expiry as expired.\n");
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --all           Sweep every brain in brains.json, not just this one\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Every command runs this after it, so you rarely need it.\n");
        (void)fprintf(out, "Human stdout: the count of memories marked.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_LINK || topic == CLI_CMD_UNLINK) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --from ID       Source entry id\n");
        (void)fprintf(out, "  --from-key KEY  Source entry key\n");
        (void)fprintf(out, "  --to ID         Target entry id\n");
        (void)fprintf(out, "  --to-key KEY    Target entry key\n");
        (void)fprintf(out, "  --kind KIND     related (default on link) | supersedes | cites\n");
        (void)fprintf(out, "  --deleted       Allow deleted ends\n");
        (void)fprintf(out, "  --as KIND       human|agent|tool|share (default human)\n");
        (void)fprintf(out, "  --actor ID      1 to 64 of A-Za-z0-9_./:-\n");
        print_hooks_help(out);
        (void)fprintf(out, "  ID ID           Sugar for --from / --to numeric ids\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Each end is exactly one of id or --*-key.\n");
        (void)fprintf(out, "Default resolves live+expired; --deleted includes deleted ends.\n");
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
        (void)fprintf(out, "  --deleted       Include deleted subject/neighbors\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key.\n");
        (void)fprintf(out, "Cannot combine --outgoing and --incoming.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_REKEY) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  ID              Entry id (positional)\n");
        (void)fprintf(out, "  --key KEY       Locate by key instead of id\n");
        print_trash_expired_help(out);
        (void)fprintf(out, "  --deleted       Locate in deleted bin\n");
        (void)fprintf(out, "  --to-key NEW    Set or rename the key (non-empty)\n");
        (void)fprintf(out, "  --clear-key     Remove the key (keyed becomes keyless)\n");
        (void)fprintf(out, "  --as KIND       human|agent|tool|share (default human)\n");
        (void)fprintf(out, "  --actor ID      1 to 64 of A-Za-z0-9_./:-\n");
        print_hooks_help(out);
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of ID or --key, and exactly one of\n");
        (void)fprintf(out, "--to-key or --clear-key. Empty --to-key is illegal (not a clear).\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_IMPORT) {
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --from-json PATH  remember-export JSON file\n");
        (void)fprintf(out, "  --from-md PATH    Markdown export folder\n");
        (void)fprintf(out, "  --from-csv PATH   CSV export file\n");
        (void)fprintf(out, "  --from-db PATH    Source remember database (read-only)\n");
        print_hooks_help(out);
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Exactly one of --from-json, --from-md, --from-csv, --from-db.\n");
        (void)fprintf(out, "Merges by id. Conflicts are recorded; exit 0 even if any.\n");
        (void)fprintf(out, "Run 'conflicts' after import. --from-db reads schema v4 to v6.\n");
        (void)fprintf(out, "Does not copy the foreign device_id sidecar.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_CONFLICTS) {
        (void)fprintf(out, "Lists unresolved import conflicts. Takes no options.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_CONFLICT) {
        (void)fprintf(out, "Usage:\n");
        (void)fprintf(out, "  remember conflict accept --id N --keep local|incoming|both\n");
        print_hooks_help(out);
        (void)fprintf(out, "\n");
        (void)fprintf(out, "both remints id on concurrent_vv; clash keeps incoming id.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_REPAIR) {
        (void)fprintf(out, "Rebuilds the search index after the index itself is damaged.\n");
        (void)fprintf(out, "  remember repair search-index\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Refuses when a base table fails its check.\n");
        (void)fprintf(out, "A missing brain fails with cannot_open. Repair creates nothing.\n");
        (void)fprintf(out, "Human stdout: rebuilt N\n");
        (void)fprintf(out, "JSON: type repair.search_index.result, data "
                           "{\"action\":\"rebuilt\",\"entries\":N}\n");
        (void)fprintf(out, "N is the number of rows in entries, in every bin.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_EXPORT) {
        (void)fprintf(out, "  remember export --format json|md|csv --out PATH [--force]\n");
        (void)fprintf(out, "Options:\n");
        (void)fprintf(out, "  --format FMT        json, md, or csv\n");
        (void)fprintf(out, "  --out PATH          The file to write (a folder for md)\n");
        (void)fprintf(out, "  --include-expired   Also export expired memories\n");
        (void)fprintf(out, "  --include-trash     Also export memories in the Trash\n");
        (void)fprintf(out, "  --exclude-archived  Accepted; no effect until archiving exists\n");
        (void)fprintf(out,
                      "  --force             Replace PATH when it exists (md: an export only)\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "Live memories by default. A link is exported when both ends are.\n");
        (void)fprintf(out, "csv also writes <name>.manifest.json beside the file.\n");
        (void)fprintf(out, "A missing brain fails with cannot_open. Export creates nothing.\n");
        (void)fprintf(out, "Human stdout: exported N memories, M links to PATH\n");
        (void)fprintf(out, "JSON: type memory.export.result, data {\"action\":\"exported\","
                           "\"format\":F,\"path\":P,\"count\":N,\"links\":M}\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_DEVICE) {
        (void)fprintf(out, "Prints the sidecar device id.\n");
        (void)fprintf(out, "  remember device\n");
        (void)fprintf(out, "  remember device new\n");
        (void)fprintf(out, "\n");
        (void)fprintf(out, "'device new' mints a new id and writes the sidecar.\n");
        (void)fprintf(out, "Run it after a restore or a copied brain.\n");
        (void)fprintf(out, "Plain output is that id. It does not change a version vector.\n");
        (void)fprintf(out, "It also replaces an invalid sidecar (no other command does).\n");
        (void)fprintf(out, "JSON adds \"previous\", the id it replaced.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_BRAIN) {
        (void)fprintf(out, "remember brain new-id replaces brain_meta.brain_id with a new "
                           "lowercase UUID v7 and prints that id.\n");
        (void)fprintf(out, "It does not change change_seq, a version vector, or a memory.\n");
        (void)fprintf(out, "--json type is brain.new_id.result.\n");
        (void)fprintf(out, "data is {brain_id, previous_brain_id}.\n");
        (void)fprintf(
            out, "remember brain list prints each brain in the list: path, brain_id, added_at.\n");
        (void)fprintf(out, "--json type is brain.list.result. data is {count, brains[]}.\n");
        (void)fprintf(out, "remember brain prune removes each path that has no brain file.\n");
        (void)fprintf(out,
                      "--json type is brain.prune.result. data is {action, count, brains[]}.\n");
        (void)fprintf(out, "\n");
    } else if (topic == CLI_CMD_SETTINGS) {
        (void)fprintf(out, "Show or change a setting.\n");
        (void)fprintf(out, "  remember settings get KEY [--scope brain|user] [--value]\n");
        (void)fprintf(out, "  remember settings set KEY VALUE... [--scope brain|user] "
                           "[--as KIND] [--actor ID]\n");
        (void)fprintf(out, "  remember settings set KEY --clear [--scope brain|user] "
                           "[--as KIND] [--actor ID]\n");
        (void)fprintf(out, "  remember settings reset KEY [--scope brain|user] "
                           "[--as KIND] [--actor ID]\n");
        (void)fprintf(out, "  remember settings reset --namespace NS [--scope brain|user] --yes\n");
        (void)fprintf(out, "  remember settings reset --all --scope brain|user --yes\n");
        (void)fprintf(out, "  remember settings list [--scope brain|user] [--namespace NS] "
                           "[--changed] [--limit N] [--cursor C] [PREFIX]\n");
        (void)fprintf(out, "  remember settings history [KEY] [--scope brain|user] [--limit N] "
                           "[--cursor C]\n");
        (void)fprintf(out, "  remember settings explain KEY | --all\n");
        (void)fprintf(out, "  remember settings export [--scope brain|user|all]\n");
        (void)fprintf(out, "  remember settings import FILE [--dry-run] [--scope brain|user]\n");
        (void)fprintf(out, "Without --scope, set and reset write the user layer unless the key "
                           "allows only the brain.\n");
        print_hooks_help(out);
        (void)fprintf(out, "--scope shows only that layer. --value prints only the value.\n");
        (void)fprintf(out, "An empty layer prints (unset).\n");
        (void)fprintf(out, "\n");
    }
    (void)fprintf(out, "Global options: --db PATH, --json, --strict, "
                       "--color=auto|always|never, --quiet, "
                       "--busy-timeout MS, --no-create, --help, --version\n");
    (void)fprintf(out, "See also: remember --help\n");
}

static DiagCode code_for_cli_error(CliError err)
{
    switch (err) {
    case CLI_ERR_MISSING_COMMAND:
    case CLI_ERR_UNKNOWN_COMMAND:
    case CLI_ERR_UNKNOWN_OPTION:
    case CLI_ERR_MISSING_OPTION_VALUE:
    case CLI_ERR_INVALID_BUSY_TIMEOUT:
    case CLI_ERR_INVALID_COLOR:
        return DIAG_USAGE;
    case CLI_ERR_OK:
    case CLI_ERR_INTERNAL:
    default:
        return DIAG_INTERNAL;
    }
}

static void print_parse_error(const CliArgs *args)
{
    const char *base = cli_error_message(args->error);
    DiagCode code = code_for_cli_error(args->error);

    switch (args->error) {
    case CLI_ERR_UNKNOWN_COMMAND:
    case CLI_ERR_UNKNOWN_OPTION:
        if (args->error_arg != NULL) {
            err_fmt(code, "%s '%s'", base, args->error_arg);
            return;
        }
        break;
    case CLI_ERR_MISSING_OPTION_VALUE:
        if (args->error_option != NULL) {
            err_fmt(code, "%s: %s", args->error_option, base);
            return;
        }
        break;
    case CLI_ERR_OK:
    case CLI_ERR_MISSING_COMMAND:
    case CLI_ERR_INVALID_BUSY_TIMEOUT:
    case CLI_ERR_INVALID_COLOR:
    case CLI_ERR_INTERNAL:
    default:
        break;
    }
    err_msg(code, base);
}

/* 007 ST-7.15: one setting_invalid warning per prefix the v6 step left out.
   The same step rewrote every memory, so no line says the brain wasn't changed. A prefix with a bad
   shape has its C-05 line and E3B-1 entry 5 fix. A folded duplicate has no approved fix yet
   (decision:open-2026-11-16-warning-fix-setting-invalid-leftovers). */
static void warn_open_settings(const Store *s)
{
    static const char k_bad_prefix_fix[] =
        "To keep it, add it again with 1 to 32 letters, digits, \"-\", \"_\" or \".\", then one "
        "\":\", like \"project:\".";
    size_t i = 0;

    for (i = 0; i < store_open_warning_count(s); i++) {
        StoreSettingWarning w = store_open_warning(s, i);
        const char *fix = NULL;
        char *line = NULL;
        if (strcmp(w.reason, "bad_prefix_shape") == 0) {
            line =
                fmt_alloc("dropped \"%s\" from \"%s\": it isn't a bucket prefix.", w.value, w.key);
            fix = k_bad_prefix_fix;
        } else {
            line = fmt_alloc("skipped \"%s\": \"%s\" isn't a valid value.", w.key, w.value);
        }
        if (line != NULL) {
            DiagDetailField fields[] = {
                {"key", w.key}, {"value", w.value}, {"reason", w.reason}, {"item", w.value}};

            (void)diag_warning_split_detail(DIAG_WARN_SETTING_INVALID,
                                            (DiagSplit){.message = line, .fix = fix}, fields,
                                            sizeof(fields) / sizeof(fields[0]));
        }
        free(line);
    }
}

/* --busy-timeout, or the store default. Zero is a real value. It bounds the open and the
   brains.json lock wait (decision:2026-11-16-plan26-review-grill G5). */
static int busy_timeout_ms_of(const CliArgs *args)
{
    if (args->globals.busy_timeout_set) {
        return args->globals.busy_timeout_ms;
    }
    return STORE_DEFAULT_BUSY_TIMEOUT_MS;
}

/* Resolve path, optional sync-path warning, then open. Store stays path-pure. */
static Store *open_store(const CliArgs *args, int *out_rc)
{
    enum { ERR_BUFSIZE = 256 }; /* store_open error-message buffer */
    char path[REMEMBER_PATH_MAX];
    char err[ERR_BUFSIZE];
    StoreError detail;
    StoreOpenOpts opts = {.busy_timeout_ms = busy_timeout_ms_of(args)};
    Store *s = NULL;

    err[0] = '\0';
    if (util_resolve_db_path(args->globals.db_path, path, sizeof(path), err, sizeof(err)) != 0) {
        const char *msg = err[0] != '\0' ? err : "invalid database path";

        err_msg(code_for_store_text(msg), msg);
        *out_rc = REMEMBER_ERR;
        return NULL;
    }
    /* Warn only: synced volumes can corrupt any journal mode; still open. */
    if (util_path_looks_synced(path)) {
        (void)diag_warning_split_detail(
            DIAG_WARN_SYNCED_FOLDER,
            (DiagSplit){.message = "this brain looks like it is in a synced folder (iCloud Drive, "
                                   "Dropbox or Google Drive), and syncing can damage it.",
                        .fix = "Move the brain to a folder on a local disk."},
            NULL, 0U);
    }
    opts.no_create = args->globals.no_create;
    /* Repair and export never create a brain (decision:plan16-repair-no-create,
       decision:plan22-export-no-create). */
    if (args->command == CLI_CMD_REPAIR || args->command == CLI_CMD_EXPORT) {
        opts.no_create = true;
    }
    if (args->command == CLI_CMD_DEVICE && cmd_device_mints(args->rest_argc, args->rest_argv)) {
        opts.replace_invalid_sidecar = true;
    }
    s = store_open_opts(path, &opts, err, sizeof(err), &detail);
    if (s == NULL) {
        /* A captured SQLite failure takes the E4.8 line; mkdir, sidecar and
           version failures keep their stage 2 code and text (no SQLite cause
           exists, so detail is the zero value). */
        if (detail.error_class != STORE_ERROR_NONE) {
            err_store_sqlite(&detail, path);
        } else {
            const char *msg = err[0] != '\0' ? err : "cannot open database";

            err_msg(code_for_store_text(msg), msg);
        }
        *out_rc = REMEMBER_ERR;
        return NULL;
    }
    warn_open_settings(s);
    if (brain_note_open(s) != 0) {
        store_close(s);
        *out_rc = REMEMBER_ERR;
        return NULL;
    }
    return s;
}

enum { ERR_TEXT_CAP = 256 };

typedef int (*CmdFn)(Store *s, bool json, int rest_argc, const char **rest_argv);

/* The class of every command (plan 27b). A command added to CliCommand and not
   here is APP_CMD_UNLISTED, which a test refuses. device new mints a sidecar
   file and writes nothing to the brain (007 ST-10.2), so it is a read. */
static const AppCommandClass k_command_class[] = {
    [CLI_CMD_NONE] = APP_CMD_NO_STORE,    [CLI_CMD_HELP] = APP_CMD_NO_STORE,
    [CLI_CMD_VERSION] = APP_CMD_NO_STORE, [CLI_CMD_UNKNOWN] = APP_CMD_NO_STORE,
    [CLI_CMD_ADD] = APP_CMD_WRITE,        [CLI_CMD_SEARCH] = APP_CMD_READ,
    [CLI_CMD_LIST] = APP_CMD_READ,        [CLI_CMD_GET] = APP_CMD_READ,
    [CLI_CMD_UPDATE] = APP_CMD_WRITE,     [CLI_CMD_DELETE] = APP_CMD_WRITE,
    [CLI_CMD_TAGS] = APP_CMD_READ,        [CLI_CMD_PURGE] = APP_CMD_WRITE,
    [CLI_CMD_LINK] = APP_CMD_WRITE,       [CLI_CMD_UNLINK] = APP_CMD_WRITE,
    [CLI_CMD_RELATED] = APP_CMD_READ,     [CLI_CMD_REKEY] = APP_CMD_WRITE,
    [CLI_CMD_IMPORT] = APP_CMD_WRITE,     [CLI_CMD_CONFLICTS] = APP_CMD_READ,
    [CLI_CMD_CONFLICT] = APP_CMD_WRITE,   [CLI_CMD_DEVICE] = APP_CMD_READ,
    [CLI_CMD_REPAIR] = APP_CMD_WRITE,     [CLI_CMD_BRAIN] = APP_CMD_WRITE,
    [CLI_CMD_EXPORT] = APP_CMD_READ,      [CLI_CMD_SETTINGS] = APP_CMD_READ,
    [CLI_CMD_SWEEP] = APP_CMD_WRITE,
};

/* settings: set, reset and import change settings; every other verb reads. */
static bool settings_verb_writes(int rest_argc, const char *const *rest_argv)
{
    CliSub sub = cli_subcommand(CLI_CMD_SETTINGS, rest_argc, rest_argv);

    return (sub == CLI_SUB_SETTINGS_SET || sub == CLI_SUB_SETTINGS_RESET ||
            sub == CLI_SUB_SETTINGS_IMPORT) != 0;
}

/* cppcheck-suppress staticFunction ; public API (remember_app.h); used by tests */
AppCommandClass app_command_class(CliCommand command, int rest_argc, const char *const *rest_argv)
{
    if ((size_t)command >= sizeof(k_command_class) / sizeof(k_command_class[0])) {
        return APP_CMD_UNLISTED;
    }
    if (command == CLI_CMD_SETTINGS && settings_verb_writes(rest_argc, rest_argv)) {
        return APP_CMD_WRITE;
    }
    /* `brain list` and `brain prune` open no brain (cmd_brain_uses_store). */
    if (command == CLI_CMD_BRAIN &&
        cli_subcommand(CLI_CMD_BRAIN, rest_argc, rest_argv) != CLI_SUB_BRAIN_NEW_ID) {
        return APP_CMD_NO_STORE;
    }
    return k_command_class[command];
}

_Static_assert(sizeof(REMEMBER_VERSION) <= DIAG_CLI_VERSION_MAX + 1U,
               "REMEMBER_VERSION is too long for the newer-brain notice");

/* A brain from a newer remember (plan 27b): a write is refused before the
   command runs, so it cannot touch the file; a read runs and warns, every time.
   The result is 0 to go on, else the exit code. */
static int meet_newer_brain(const Store *s, const CliArgs *args)
{
    StoreSchemaGap gap = {.brain_schema = 0};

    if (!store_newer_brain(s, &gap)) {
        return 0;
    }
    if (app_command_class(args->command, args->rest_argc, args->rest_argv) == APP_CMD_WRITE) {
        (void)diag_schema_too_new_error(gap, REMEMBER_VERSION);
        return REMEMBER_TEMPFAIL;
    }
    (void)diag_schema_too_new_warning(gap, REMEMBER_VERSION);
    return 0;
}

/* The keys the brain's settings calls know: the compiled-in rows, plugin.hybrid.enabled, and
   the keys of each plugin in <data dir>/plugins (plan 30). With no data dir (HOME and
   XDG_DATA_HOME unset) no plugin is installed, as far as anyone can tell. mans gets the
   manifests behind those keys (plan 31). 0, or the exit code. */
static int load_registry(SettingsRegistry **out, PluginManifestList *mans)
{
    char dir[REMEMBER_PATH_MAX];
    char err[ERR_TEXT_CAP];
    int sys_errno = 0;
    PluginSettingsStatus st = PLUGIN_SETTINGS_OK;
    SettingsRegistry *reg = settings_registry_new();

    *out = reg;
    if (reg == NULL || plugin_settings_register_hybrid(reg) != PLUGIN_SETTINGS_OK) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return REMEMBER_ERR;
    }
    if (util_resolve_data_dir(dir, sizeof(dir) - sizeof("/plugins"), err, sizeof(err)) != 0) {
        return 0;
    }
    /* The data dir left room for the folder name. */
    (void)snprintf(dir + strlen(dir), sizeof("/plugins"), "/plugins");
    st = plugin_settings_load_manifests(dir, reg, mans, &sys_errno);
    if (st == PLUGIN_SETTINGS_OOM) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return REMEMBER_ERR;
    }
    if (st == PLUGIN_SETTINGS_DIR) {
        err_msg(DIAG_CANNOT_OPEN, "cannot read the plugins folder");
        return REMEMBER_ERR;
    }
    return 0;
}

/* ---- memory events (plan 31) ------------------------------------------------------------ */

/* cause.command (R3.2). conflict accept has no value of its own yet
   (decision:open-2026-11-18-plan31-conflict-accept-command): it reports update. NULL when the
   command changes no memory. */
static const char *event_command_name(CliCommand command)
{
    static const bool k_writes[] = {
        [CLI_CMD_ADD] = true,   [CLI_CMD_UPDATE] = true, [CLI_CMD_DELETE] = true,
        [CLI_CMD_PURGE] = true, [CLI_CMD_LINK] = true,   [CLI_CMD_UNLINK] = true,
        [CLI_CMD_REKEY] = true, [CLI_CMD_IMPORT] = true, [CLI_CMD_CONFLICT] = true,
        [CLI_CMD_SWEEP] = true,
    };

    if ((size_t)command >= sizeof(k_writes) / sizeof(k_writes[0]) || !k_writes[command]) {
        return NULL;
    }
    return command == CLI_CMD_CONFLICT ? "update" : cli_command_name(command);
}

/* What one command's events hold; the store keeps pointers into it until store_close. */
typedef struct {
    EventRules rules;
    EventCause cause;
    char brain_label[REMEMBER_PATH_MAX];
    const char *command;
    StoreOrigin origin;
    bool loaded; /* rules and cause are read */
    bool armed;
    bool warned_unsupported; /* plugin_unsupported is printed once per command */
    char pad_[1];
} CommandEvents;

/* The brain path as an event shows it: absolute, with the home folder written ~ (OQ-14). */
static void brain_label(const char *path, char *out, size_t cap)
{
    char absolute[REMEMBER_PATH_MAX];
    char home[REMEMBER_PATH_MAX];
    const char *raw_home = getenv("HOME"); // NOLINT(concurrency-mt-unsafe) single-threaded CLI
    const char *shown = path; /* when it cannot be made absolute (too long), as given */
    size_t n = 0U;

    /* HOME is compared in the same normal form as the path ("/Users/x/" is "/Users/x"). */
    if (raw_home != NULL && raw_home[0] == '/' &&
        util_absolute_path(raw_home, home, sizeof(home)) == 0) {
        n = strlen(home);
    }
    if (util_absolute_path(path, absolute, sizeof(absolute)) == 0) {
        shown = absolute;
    }
    if (n > 1U && strncmp(shown, home, n) == 0 && shown[n] == '/') {
        (void)snprintf(out, cap, "~%s", shown + n);
    } else {
        (void)snprintf(out, cap, "%s", shown);
    }
}

/* Read the rules and the cause the first time a command arms the store. 0, or the exit code. */
static int load_events(Store *s, const PluginManifestList *mans, CommandEvents *cev, bool report)
{
    char userdb[REMEMBER_PATH_MAX];
    char err[ERR_TEXT_CAP];
    const char *user_db = NULL;

    if (cev->loaded) {
        return 0;
    }
    if (util_resolve_user_db_path(userdb, sizeof(userdb), err, sizeof(err)) == 0) {
        user_db = userdb;
    }
    if (events_rules_load(s, user_db, mans, &cev->rules, err, sizeof(err)) != 0) {
        if (report) {
            err_msg(code_for_store_text(err), err);
        }
        return REMEMBER_ERR;
    }
    if (events_cause_read(&cev->cause) != 0) {
        if (report) {
            err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        }
        return REMEMBER_ERR;
    }
    brain_label(store_path(s), cev->brain_label, sizeof(cev->brain_label));
    cev->loaded = true;
    return 0;
}

/* Turn on events for what the store writes next, as the command named, when an enabled plugin
   has an action. A command that changes memories arms with its own name; the sweep after a
   command arms again as "sweep", with the rules it already read. NULL command: nothing changes
   memories. 0, or the exit code; report says whether the failure is printed (the sweep after a
   command prints its own warning instead). */
static int arm_events(Store *s, const char *command, StoreOrigin origin,
                      const PluginManifestList *mans, CommandEvents *cev, bool report)
{
    StoreEvents cfg;
    StoreStatus st = STORE_OK;
    int rc = 0;

    cev->command = command;
    if (command == NULL || mans->count == 0U) {
        return 0;
    }
    rc = load_events(s, mans, cev, report);
    if (rc != 0) {
        return rc;
    }
    cev->origin = origin;
    memset(&cfg, 0, sizeof(cfg));
    cfg.rules = &cev->rules;
    cfg.cause = &cev->cause;
    cfg.command = cev->command;
    cfg.brain_path = cev->brain_label;
    cfg.origin = cev->origin;
    cfg.hooks = STORE_HOOKS_DEFAULT;
    st = store_set_events(s, &cfg);
    if (st != STORE_OK) {
        if (report) {
            err_store(s, st);
        }
        return REMEMBER_ERR;
    }
    cev->armed = true;
    return 0;
}

/* After a command that succeeded: the warning for a write that needed a program this build cannot
   start, and the skipped_source lines for a sync or import. A command that failed may have rolled
   its write back, and then neither is true. The warning prints once per command, however many
   writes (the command's, then its sweep's) it covers. */
static void finish_events(const Store *s, CommandEvents *cev, int rc)
{
    if (!cev->armed || rc != 0) {
        return;
    }
    if (store_events_unsupported(s) > 0U && !cev->warned_unsupported) {
        cev->warned_unsupported = true;
        (void)diag_plugin_unsupported();
    }
    (void)events_log_skipped(&cev->rules, store_path(s), cev->command, cev->origin);
}

static void command_events_free(CommandEvents *cev)
{
    events_rules_free(&cev->rules);
    if (cev->cause.chain_json != NULL) {
        events_cause_free(&cev->cause);
    }
}

/* The sweep after a command (log 010 H-1.5): every command that opened the brain read-write sweeps
   once its own work is done, whatever its exit, and a brain this process cannot write (a newer
   one) is skipped. The sweep is its own write, so its events are cause.command "sweep" in their
   own batch. A sweep that fails is a warning and changes no exit code, unless the command itself
   failed: then its own error is the one message
   (decision:2026-11-18-plan32-grill-sweep-triggers,
   decision:2026-11-18-plan32-grill-sweep-warning-and-wrong-bin). */
static void sweep_after(Store *s, const PluginManifestList *mans, CommandEvents *cev,
                        int command_rc)
{
    size_t marked = 0U;

    if (!store_is_writable(s)) {
        return;
    }
    /* A sweep that cannot arm its events must not mark rows: their events would be lost. */
    if (arm_events(s, "sweep", STORE_ORIGIN_LOCAL, mans, cev, false) != 0 ||
        sweep_now(s, &marked) != STORE_OK) {
        if (command_rc == 0) {
            (void)diag_sweep_failed(store_path(s));
        }
        return;
    }
    finish_events(s, cev, 0);
}

static int run_with_store(const CliArgs *args, CmdFn fn)
{
    Store *s = NULL;
    SettingsRegistry *reg = NULL;
    PluginManifestList mans = {.items = NULL, .count = 0U};
    CommandEvents cev;
    int rc = 0;

    memset(&cev, 0, sizeof(cev));
    s = open_store(args, &rc);
    if (s == NULL) {
        return rc;
    }
    rc = load_registry(&reg, &mans);
    if (rc == 0) {
        store_set_settings_registry(s, reg);
        rc = meet_newer_brain(s, args);
    }
    if (rc == 0) {
        rc = arm_events(s, event_command_name(args->command),
                        args->command == CLI_CMD_IMPORT ? STORE_ORIGIN_IMPORT : STORE_ORIGIN_LOCAL,
                        &mans, &cev, true);
    }
    if (rc == 0) {
        int fn_rc = fn(s, args->globals.json, args->rest_argc, args->rest_argv);

        finish_events(s, &cev, fn_rc);
        if (args->command != CLI_CMD_SWEEP) {
            sweep_after(s, &mans, &cev, fn_rc);
        }
        rc = fn_rc;
    }
    store_close(s);
    command_events_free(&cev);
    plugin_manifest_list_free(&mans);
    settings_registry_free(reg);
    return rc;
}

/* What sweep --all shares between the brains it sweeps: the plugins and settings registry are
   the machine's, read once. */
typedef struct {
    const CliArgs *args;
    SettingsRegistry *reg;
    const PluginManifestList *mans;
} SweepAll;

static int sweep_listed_brain(const void *ctx, const char *path, size_t *marked)
{
    const SweepAll *all = (const SweepAll *)ctx;
    StoreOpenOpts opts = {.busy_timeout_ms = busy_timeout_ms_of(all->args), .no_create = true};
    char err[ERR_TEXT_CAP];
    CommandEvents cev;
    Store *s = store_open_opts(path, &opts, err, sizeof(err), NULL);
    StoreStatus st = STORE_OK;

    memset(&cev, 0, sizeof(cev));
    if (s == NULL) {
        (void)diag_sweep_failed(path);
        return -1;
    }
    store_set_settings_registry(s, all->reg);
    /* A newer brain is not written: store_set_events arms nothing there and sweep_now marks
       nothing (specs/OLDER-CLI.md). */
    st = arm_events(s, "sweep", STORE_ORIGIN_LOCAL, all->mans, &cev, false) == 0
             ? sweep_now(s, marked)
             : STORE_ERR_INTERNAL;
    if (st == STORE_OK) {
        finish_events(s, &cev, 0);
    } else {
        (void)diag_sweep_failed(path);
    }
    store_close(s);
    command_events_free(&cev);
    return st == STORE_OK ? 0 : -1;
}

/* `sweep --all`: reads brains.json and sweeps each listed brain. It opens no brain for itself. */
static int run_sweep_all(const CliArgs *args)
{
    SettingsRegistry *reg = NULL;
    PluginManifestList mans = {.items = NULL, .count = 0U};
    int rc = load_registry(&reg, &mans);

    if (rc == 0) {
        SweepAll all = {.args = args, .reg = reg, .mans = &mans};

        rc = cmd_sweep_all(args->globals.json, sweep_listed_brain, &all);
    }
    plugin_manifest_list_free(&mans);
    settings_registry_free(reg);
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
    case CLI_CMD_EXPORT:
        if (cmd_export_reject_args(args->rest_argc, args->rest_argv) != 0) {
            return REMEMBER_ERR;
        }
        return run_with_store(args, cmd_export);
    case CLI_CMD_CONFLICTS:
        return run_with_store(args, cmd_conflicts);
    case CLI_CMD_CONFLICT:
        return run_with_store(args, cmd_conflict);
    case CLI_CMD_DEVICE:
        return run_with_store(args, cmd_device);
    case CLI_CMD_REPAIR:
        if (cmd_repair_reject_args(args->rest_argc, args->rest_argv) != 0) {
            return REMEMBER_ERR;
        }
        return run_with_store(args, cmd_repair);
    case CLI_CMD_BRAIN:
        if (cmd_brain_reject_args(args->rest_argc, args->rest_argv) != 0) {
            return REMEMBER_ERR;
        }
        if (!cmd_brain_uses_store(args->rest_argc, args->rest_argv)) {
            return cmd_brain_file(args->globals.json, busy_timeout_ms_of(args), args->rest_argc,
                                  args->rest_argv);
        }
        return run_with_store(args, cmd_brain);
    case CLI_CMD_SETTINGS:
        return run_with_store(args, cmd_settings);
    case CLI_CMD_SWEEP: {
        bool all = false;

        if (sweep_parse(args->rest_argc, args->rest_argv, &all) != 0) {
            return REMEMBER_ERR;
        }
        if (all) {
            return run_sweep_all(args);
        }
        return run_with_store(args, cmd_sweep);
    }
    case CLI_CMD_NONE:
        /* Defensive: parse should set CLI_ERR_MISSING_COMMAND first. */
        err_msg(DIAG_USAGE, cli_error_message(CLI_ERR_MISSING_COMMAND));
        return REMEMBER_ERR;
    case CLI_CMD_UNKNOWN:
        /* Defensive: parse should set CLI_ERR_UNKNOWN_COMMAND first. */
        err_msg(DIAG_USAGE, cli_error_message(CLI_ERR_UNKNOWN_COMMAND));
        return REMEMBER_ERR;
    default:
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
}

/* <noun>.<verb> of the request's reply type (JSON-STYLE 1.7a), the one place
   it is named (decision:plan21-envelope-closed-by-app). "unknown" when the CLI
   cannot tell the command. */
static const char *reply_noun_verb(const CliArgs *args)
{
    switch (args->command) {
    case CLI_CMD_ADD:
        return "memory.add";
    case CLI_CMD_SEARCH:
        return "memory.search";
    case CLI_CMD_LIST:
        return "memory.list";
    case CLI_CMD_GET:
        return "memory.get";
    case CLI_CMD_UPDATE:
        return "memory.update";
    case CLI_CMD_DELETE:
        return "memory.delete";
    case CLI_CMD_TAGS:
        return "tag.list";
    case CLI_CMD_PURGE:
        return "memory.purge";
    case CLI_CMD_SWEEP:
        return "sweep";
    case CLI_CMD_LINK:
        return "memory.link";
    case CLI_CMD_UNLINK:
        return "memory.unlink";
    case CLI_CMD_RELATED:
        return "memory.related";
    case CLI_CMD_REKEY:
        return "memory.rekey";
    case CLI_CMD_IMPORT:
        return "memory.import";
    case CLI_CMD_EXPORT:
        return "memory.export";
    case CLI_CMD_CONFLICTS:
        return "conflict.list";
    case CLI_CMD_CONFLICT:
        return "conflict.accept";
    case CLI_CMD_DEVICE:
        if (cmd_device_mints(args->rest_argc, args->rest_argv)) {
            return "device.new";
        }
        return "device.get";
    case CLI_CMD_REPAIR:
        return "repair.search_index";
    case CLI_CMD_BRAIN:
        return cmd_brain_reply(args->rest_argc, args->rest_argv);
    case CLI_CMD_SETTINGS:
        return cmd_settings_reply(args->rest_argc, args->rest_argv);
    case CLI_CMD_NONE:
    case CLI_CMD_HELP:
    case CLI_CMD_VERSION:
    case CLI_CMD_UNKNOWN:
    default:
        return "unknown";
    }
}

/* Help and version print text even with --json. */
static bool writes_envelope(const CliArgs *args)
{
    if (!args->globals.json) {
        return false;
    }
    if (args->error == CLI_ERR_OK &&
        (args->command == CLI_CMD_HELP || args->command == CLI_CMD_VERSION)) {
        return false;
    }
    return true;
}

/* next_cursor and has_more travel together, only when the list set a page.
   A NULL cursor on a set page is JSON null (the last page). */
static int write_page_fields(FILE *out, bool page, const char *cursor)
{
    if (!page) {
        return 0;
    }
    if (fputs(",\"next_cursor\":", out) < 0) {
        return -1;
    }
    if (cursor == NULL) {
        if (fputs("null", out) < 0) {
            return -1;
        }
    } else if (output_json_string(out, cursor) != 0) {
        return -1;
    }
    return fputs(cursor != NULL ? ",\"has_more\":true" : ",\"has_more\":false", out) < 0 ? -1 : 0;
}

/* The envelope around the buffered data, or around the run's first error:
   {format, version, type, data | error, next_cursor?, has_more?, warnings?} (JE-2.1). */
static int write_envelope(FILE *out, const char *noun_verb, bool failed, const char *data,
                          size_t len, bool page, const char *cursor)
{
    if (fprintf(out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"%s.result\",",
                noun_verb) < 0) {
        return -1;
    }
    if (failed) {
        if (diag_json_write_error(out) != 0) {
            return -1;
        }
    } else if (fputs("\"data\":", out) < 0 || fwrite(data, 1U, len, out) != len) {
        return -1;
    }
    if (write_page_fields(out, page, cursor) != 0 || diag_json_write_warnings(out) != 0 ||
        fputs("}\n", out) < 0) {
        return -1;
    }
    return 0;
}

/* --json: the command writes its data object into a buffer and diag buffers
   notices, then one envelope goes to out and stderr stays empty (JE-2.8). A
   failure keeps the type and drops any data. */
static int run_json(const CliArgs *args, AppStreams streams)
{
    FILE *out = streams.out;
    const char *noun_verb = reply_noun_verb(args);
    char *data = NULL;
    size_t len = 0U;
    FILE *data_fp = open_memstream(&data, &len);
    char *cursor = NULL;
    bool page = false;
    int rc = REMEMBER_ERR;

    if (data_fp == NULL || diag_json_begin(cli_command_name(args->command)) != 0) {
        (void)fprintf(out, k_out_of_memory_reply, noun_verb, diag_code_fix(DIAG_OUT_OF_MEMORY));
        goto cleanup;
    }
    /* Keep the caller's error stream. diag.c is the only writer of it. */
    app_set_streams((AppStreams){.out = data_fp, .err = streams.err});
    rc = run(args);
    if (fflush(data_fp) != 0 || ferror(data_fp) != 0) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
    }
    /* A failure with no report of its own still gets one error. */
    if (rc != REMEMBER_OK && !diag_json_has_error()) {
        err_msg(DIAG_INTERNAL, "internal error");
    }
    rc = (rc == REMEMBER_OK && diag_json_has_error()) ? REMEMBER_ERR : rc;
    page = app_take_list_page(&cursor);
    if (app_take_document() && rc == REMEMBER_OK && !diag_json_has_error()) {
        /* A document is its own format (decision:plan20-export-json-error). */
        if (fwrite(data, 1U, len, out) != len || fflush(out) != 0 || ferror(out) != 0) {
            diag_json_end();
            err_msg(DIAG_INTERNAL, "failed to write output");
            rc = REMEMBER_ERR;
        }
        goto cleanup;
    }
    if (rc != REMEMBER_OK || diag_json_has_error()) {
        page = false;
        free(cursor);
        cursor = NULL;
    }
    if (write_envelope(out, noun_verb, diag_json_has_error(), data, len, page, cursor) != 0 ||
        fflush(out) != 0 || ferror(out) != 0) {
        /* stdout is gone: stderr is the one place left to say so. */
        diag_json_end();
        err_msg(DIAG_INTERNAL, "failed to write output");
        rc = REMEMBER_ERR;
    }
cleanup:
    diag_json_end();
    if (data_fp != NULL) {
        (void)fclose(data_fp);
    }
    free(data);
    free(cursor);
    return rc;
}

/* cppcheck-suppress staticFunction ; public entry point (remember_app.h); used by main + GUI +
 * tests */
int remember_run(int argc, char *const *argv, FILE *out, FILE *err)
{
    CliArgs args;
    int rc = 0;

    diag_warnings_reset();
    app_clear_list_page();
    (void)app_take_document();
    app_set_streams((AppStreams){.out = out, .err = err});
    cli_parse(argc, argv, &args);
    {
        AppPresentation presentation = {
            .color = args.globals.color,
            .quiet = args.globals.quiet,
            .json = args.globals.json,
            .pad_ = {'\0', '\0'},
        };

        /* Left in place so a caller can read the mode after this run.
           The next remember_run replaces it. */
        app_set_presentation(presentation);
    }
    if (writes_envelope(&args)) {
        rc = run_json(&args, (AppStreams){.out = out, .err = err});
    } else {
        rc = run(&args);
        /* Buffered output is written here or at exit, after every command's own
           write checks; a failure then would otherwise exit 0 with nothing written.
           A failed command already reported its error, so it keeps that one line. */
        if (rc == REMEMBER_OK && (fflush(out) != 0 || ferror(out) != 0)) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            rc = REMEMBER_ERR;
        }
    }
    /* G2-03: any warning under --strict exits 4, including when the command also failed. */
    if (args.globals.strict && diag_warning_count() > 0U) {
        rc = REMEMBER_STRICT;
    }
    cli_args_free(&args);
    app_clear_list_page();
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    return rc;
}

int remember_main(int argc, char *const *argv)
{
    return remember_run(argc, argv, stdout, stderr);
}
