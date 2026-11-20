#include "cli.h"
#include "harness.h"
#include "register.h"
#include "remember_app.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The engine verb table (decision:2026-11-18-verb-ladder). Every top-level verb
   (k_commands) and every subcommand (k_subcommands) in src/cli.c has a row with
   plan `main` in specs/API-SURFACE.md; every such row names a verb or a
   subcommand that src/cli.c has; and its Rights equal app_command_class. */

enum {
    VT_CELLS = 7,         /* columns of the Verbs table */
    VT_CELL_MAX = 512,    /* one cell, unescaped */
    VT_MAX_WORDS = 4,     /* `remember a b c d` */
    VT_REPORT_MAX = 8192, /* every mismatch line of one check */
    VT_COL_COMMAND = 0,
    VT_COL_RIGHTS = 2,
    VT_COL_PLAN = 5
};

static const char k_header[] = "| CLI command |";
static const char k_rule[] = "|---";

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    int count;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} Report;

static void report_add(Report *r, const char *what, const char *subject)
{
    int n = snprintf(r->buf + r->len, r->cap - r->len, "%s: %s\n", what,
                     subject != NULL ? subject : "(unnamed)");

    r->count++;
    if (n > 0 && (size_t)n < r->cap - r->len) {
        r->len += (size_t)n;
    }
}

/* Splits one table line into cells. A `\|` inside a cell is a literal bar.
   Returns the cell count, or -1 when the line is not `| a | b | ... |`. */
static int split_cells(const char *line, size_t len, char cells[][VT_CELL_MAX], int max)
{
    size_t i = 1;
    int n = 0;
    size_t w = 0;

    if (len < 2 || line[0] != '|' || line[len - 1] != '|') {
        return -1;
    }
    while (i < len) {
        char c = line[i];
        if (c == '\\' && i + 1 < len && line[i + 1] == '|') {
            c = '|';
            i++;
        } else if (c == '|') {
            if (n >= max) {
                return -1;
            }
            while (w > 0 && cells[n][w - 1] == ' ') {
                w--;
            }
            cells[n][w] = '\0';
            n++;
            w = 0;
            i++;
            continue;
        }
        if (n < max && (w > 0 || c != ' ') && w + 1 < VT_CELL_MAX) {
            cells[n][w++] = c;
        }
        i++;
    }
    return n;
}

/* "`remember brain list`" -> words {"brain", "list"} in buf. Returns the word
   count, or 0 when the cell is not one code span that starts `remember `, has
   one space between words and at most VT_MAX_WORDS words. */
static int command_words(const char *cell, char buf[VT_CELL_MAX], const char *words[VT_MAX_WORDS])
{
    static const char k_prefix[] = "`remember ";
    size_t len = strlen(cell);
    int n = 0;
    char *p = buf;

    if (len < sizeof(k_prefix) || strncmp(cell, k_prefix, sizeof(k_prefix) - 1) != 0 ||
        cell[len - 1] != '`') {
        return 0;
    }
    (void)snprintf(buf, VT_CELL_MAX, "%.*s", (int)(len - sizeof(k_prefix)),
                   cell + sizeof(k_prefix) - 1);
    if (strchr(buf, '`') != NULL) {
        return 0;
    }
    while (*p != '\0') {
        if (*p == ' ' || n == VT_MAX_WORDS) {
            return 0;
        }
        words[n++] = p;
        p = strchr(p, ' ');
        if (p == NULL) {
            break;
        }
        *p++ = '\0';
    }
    return n;
}

static CliCommand dispatch_lookup(const char *name)
{
    int cmd = 0;

    for (cmd = CLI_CMD_NONE + 1; cmd < CLI_CMD_UNKNOWN; cmd++) {
        const char *have = cli_command_name((CliCommand)cmd);
        if (have != NULL && strcmp(have, name) == 0) {
            return (CliCommand)cmd;
        }
    }
    return CLI_CMD_UNKNOWN;
}

/* What the engine does to the brain for this verb, in the spec's words. */
static const char *engine_rights(CliCommand cmd, int argc, const char *const *argv)
{
    switch (app_command_class(cmd, argc, argv)) {
    case APP_CMD_NO_STORE:
        return "none";
    case APP_CMD_READ:
        return "read";
    case APP_CMD_WRITE:
        return "read-write";
    case APP_CMD_UNLISTED:
    default:
        return "unlisted";
    }
}

typedef struct {
    bool cmd[CLI_CMD_UNKNOWN + 1];
    bool sub[CLI_SUB_COUNT];
} Seen;

// cppcheck-suppress constParameter ; C11 will not pass char[][N] as const char[][N]
static void check_row(char cells[][VT_CELL_MAX], const char *line, Seen *seen, Report *r)
{
    const char *words[VT_MAX_WORDS] = {NULL};
    const char *name = cells[VT_COL_COMMAND];
    const char *rights = cells[VT_COL_RIGHTS];
    char buf[VT_CELL_MAX];
    int n = command_words(name, buf, words);
    CliCommand cmd = CLI_CMD_UNKNOWN;

    if (n == 0) {
        report_add(r, "row has no `remember ...` command", line);
        return;
    }
    if (strcmp(rights, "none") != 0 && strcmp(rights, "read") != 0 &&
        strcmp(rights, "read-write") != 0) {
        report_add(r, "rights is not none, read or read-write", name);
        return;
    }
    if (strcmp(cells[VT_COL_PLAN], "main") != 0) {
        return;
    }
    cmd = dispatch_lookup(words[0]);
    if (cmd == CLI_CMD_UNKNOWN) {
        report_add(r, "plan main row names a verb the dispatch table lacks", words[0]);
        return;
    }
    seen->cmd[cmd] = true;
    if (n > 2) {
        report_add(r, "plan main row has more than one subcommand", name);
        return;
    }
    if (n == 2) {
        CliSub sub = cli_subcommand(cmd, 1, words + 1);

        if (sub == CLI_SUB_UNKNOWN) {
            report_add(r, "plan main row names a subcommand the CLI lacks", name);
            return;
        }
        seen->sub[sub] = true;
    }
    if (strcmp(engine_rights(cmd, n - 1, words + 1), rights) != 0) {
        report_add(r, "rights differ from the engine", name);
    }
}

/* The verb that a subcommand belongs to, then the subcommand, for a report. */
static void report_sub(Report *r, CliSub sub)
{
    char both[VT_CELL_MAX];

    (void)snprintf(both, sizeof(both), "%s %s", cli_command_name(cli_subcommand_parent(sub)),
                   cli_subcommand_name(sub));
    report_add(r, "subcommand has no plan main row", both);
}

/* Every mismatch between src/cli.c and the `## Verbs` table of spec, one line
   each in out; returns their count. */
static int verb_table_mismatches(const char *spec, char *out, size_t outlen)
{
    char cells[VT_CELLS + 1][VT_CELL_MAX];
    Seen seen;
    Report r = {.buf = out, .cap = outlen, .len = 0, .count = 0, .pad_ = {0}};
    const char *p = spec != NULL ? strstr(spec, "\n## Verbs\n") : NULL;
    const char *end = NULL;
    int i = 0;

    memset(&seen, 0, sizeof(seen));
    out[0] = '\0';
    if (p == NULL) {
        report_add(&r, "spec has no section", "## Verbs");
        return r.count;
    }
    p += strlen("\n## Verbs\n");
    end = strstr(p, "\n## ");
    if (end == NULL) {
        end = p + strlen(p);
    }
    while (p < end) {
        const char *newline = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t len = (newline != NULL ? (size_t)(newline - p) : (size_t)(end - p));
        char line[VT_CELL_MAX * 2];

        (void)snprintf(line, sizeof(line), "%.*s", (int)len, p);
        if (len > 0 && p[0] == '|' && strncmp(p, k_header, sizeof(k_header) - 1) != 0 &&
            strncmp(p, k_rule, sizeof(k_rule) - 1) != 0) {
            if (split_cells(p, len, cells, VT_CELLS + 1) != VT_CELLS) {
                report_add(&r, "row does not have 7 cells", line);
            } else {
                check_row(cells, line, &seen, &r);
            }
        } else if (len > 0 && strchr(line, '|') != NULL && strstr(line, "remember ") != NULL) {
            report_add(&r, "line names a verb but is not a table row", line);
        }
        p += len + 1;
    }
    for (i = CLI_CMD_NONE + 1; i < CLI_CMD_UNKNOWN; i++) {
        if (!seen.cmd[i]) {
            report_add(&r, "dispatch verb has no plan main row", cli_command_name((CliCommand)i));
        }
    }
    for (i = CLI_SUB_UNKNOWN + 1; i < CLI_SUB_COUNT; i++) {
        if (!seen.sub[i]) {
            report_sub(&r, (CliSub)i);
        }
    }
    return r.count;
}

/* The real spec (heap), or NULL after a failed assert. */
static char *given_spec(void)
{
    size_t len = 0;
    char *spec = harness_read_file(REMEMBER_SOURCE_DIR "/specs/API-SURFACE.md", &len);

    ASSERT_TRUE(spec != NULL);
    return spec;
}

/* spec with the cut bytes at `at` replaced by `to` (heap). Takes spec. */
static char *splice(char *spec, const char *at, size_t cut, const char *to)
{
    size_t len = strlen(spec) - cut + strlen(to) + 1;
    char *out = (char *)malloc(len);

    ASSERT_TRUE(out != NULL);
    if (out != NULL) {
        (void)snprintf(out, len, "%.*s%s%s", (int)(at - spec), spec, to, at + cut);
    }
    free(spec);
    return out;
}

/* spec with its first `from` replaced by `to`. Takes spec; NULL after a
   failed assert. */
static char *spec_edit(char *spec, const char *from, const char *to)
{
    const char *at = spec != NULL ? strstr(spec, from) : NULL;

    ASSERT_TRUE(at != NULL);
    if (at == NULL) {
        free(spec);
        return NULL;
    }
    return splice(spec, at, strlen(from), to);
}

/* spec with `from` replaced by `to` inside the row whose command cell is
   `remember <command>`. Takes spec; NULL after a failed assert. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the row, then from -> to
static char *row_edit(char *spec, const char *command, const char *from, const char *to)
{
    char anchor[VT_CELL_MAX];
    const char *row = NULL;
    const char *row_end = NULL;
    const char *at = NULL;

    (void)snprintf(anchor, sizeof(anchor), "\n| `remember %s` |", command);
    row = spec != NULL ? strstr(spec, anchor) : NULL;
    row_end = row != NULL ? strchr(row + 1, '\n') : NULL;
    at = row != NULL ? strstr(row + 1, from) : NULL;
    ASSERT_TRUE(at != NULL && row_end != NULL && at < row_end);
    if (at == NULL || row_end == NULL || at >= row_end) {
        free(spec);
        return NULL;
    }
    return splice(spec, at, strlen(from), to);
}

static char *given_row_edit(const char *command, const char *from, const char *to)
{
    return row_edit(given_spec(), command, from, to);
}

/* The count of mismatches is exact, and the report names the expected one. */
static void assert_mismatch(const char *spec, int want, const char *needle)
{
    char out[VT_REPORT_MAX];

    ASSERT_TRUE(spec != NULL);
    ASSERT_EQ_INT(verb_table_mismatches(spec, out, sizeof(out)), want);
    ASSERT_STR_CONTAINS(out, needle);
}

TEST(cli_verbs_match_api_surface)
{
    char out[VT_REPORT_MAX];
    char *spec = given_spec();

    ASSERT_EQ_INT(verb_table_mismatches(spec, out, sizeof(out)), 0);
    ASSERT_STREQ(out, "");
    free(spec);
}

/* Each entry of the subcommand table is found under its own parent and no
   other; no argument is NONE, a stranger is UNKNOWN. */
TEST(cli_subcommand_table_lookup)
{
    const char *list[] = {"list"};
    const char *null_arg[] = {NULL};
    int i = 0;

    for (i = CLI_SUB_UNKNOWN + 1; i < CLI_SUB_COUNT; i++) {
        const char *name = cli_subcommand_name((CliSub)i);

        ASSERT_TRUE(name != NULL);
        ASSERT_EQ_INT(cli_subcommand(cli_subcommand_parent((CliSub)i), 1, &name), i);
    }
    ASSERT_EQ_INT(cli_subcommand(CLI_CMD_BRAIN, 1, list), CLI_SUB_BRAIN_LIST);
    ASSERT_EQ_INT(cli_subcommand(CLI_CMD_SETTINGS, 1, list), CLI_SUB_SETTINGS_LIST);
    ASSERT_EQ_INT(cli_subcommand(CLI_CMD_DEVICE, 1, list), CLI_SUB_UNKNOWN);
    ASSERT_EQ_INT(cli_subcommand(CLI_CMD_BRAIN, 0, list), CLI_SUB_NONE);
    ASSERT_EQ_INT(cli_subcommand(CLI_CMD_BRAIN, 1, NULL), CLI_SUB_NONE);
    ASSERT_EQ_INT(cli_subcommand(CLI_CMD_BRAIN, 1, null_arg), CLI_SUB_NONE);
    ASSERT_TRUE(cli_subcommand_name(CLI_SUB_NONE) == NULL);
    ASSERT_TRUE(cli_subcommand_name(CLI_SUB_UNKNOWN) == NULL);
    ASSERT_TRUE(cli_subcommand_name(CLI_SUB_COUNT) == NULL);
    ASSERT_EQ_INT(cli_subcommand_parent(CLI_SUB_UNKNOWN), CLI_CMD_UNKNOWN);
    ASSERT_EQ_INT(cli_subcommand_parent(CLI_SUB_COUNT), CLI_CMD_UNKNOWN);
}

/* `brain list` and `brain prune` open no brain; `brain new-id` writes it. */
TEST(cli_brain_class_follows_the_subcommand)
{
    const char *list[] = {"list"};
    const char *prune[] = {"prune"};
    const char *new_id[] = {"new-id"};

    ASSERT_EQ_INT(app_command_class(CLI_CMD_BRAIN, 1, list), APP_CMD_NO_STORE);
    ASSERT_EQ_INT(app_command_class(CLI_CMD_BRAIN, 1, prune), APP_CMD_NO_STORE);
    ASSERT_EQ_INT(app_command_class(CLI_CMD_BRAIN, 1, new_id), APP_CMD_WRITE);
}

/* A top-level verb with no row: `tags` renamed to `tagz`. */
TEST(cli_verbs_dispatch_verb_without_row_fails)
{
    char *spec = given_row_edit("tags", "`remember tags`", "`remember tagz`");

    assert_mismatch(spec, 2, "dispatch verb has no plan main row: tags\n");
    free(spec);
}

/* A shipped row that src/cli.c lacks: a top-level verb, then a subcommand. */
TEST(cli_verbs_main_row_without_dispatch_verb_fails)
{
    char *verb = given_row_edit("sync peers", "| 44 |", "| main |");
    char *sub = given_row_edit("settings history", "`remember settings history`",
                               "`remember settings histories`");

    assert_mismatch(verb, 1, "plan main row names a verb the dispatch table lacks: sync\n");
    assert_mismatch(sub, 2,
                    "plan main row names a subcommand the CLI lacks: `remember settings "
                    "histories`\n");
    assert_mismatch(sub, 2, "subcommand has no plan main row: settings history\n");
    free(verb);
    free(sub);
}

/* A subcommand with no row: `device new` and `conflict accept` planned. */
TEST(cli_verbs_subcommand_without_row_fails)
{
    char *device = given_row_edit("device new", "| main |", "| 99 |");
    char *conflict = given_row_edit("conflict accept", "`remember conflict accept`",
                                    "`remember conflict resolve`");

    assert_mismatch(device, 1, "subcommand has no plan main row: device new\n");
    assert_mismatch(conflict, 2, "subcommand has no plan main row: conflict accept\n");
    free(device);
    free(conflict);
}

/* Rights: a read marked read-write, a write marked read, a no-brain verb
   marked read, a settings write marked read; a word outside the vocabulary
   fails on a planned row too. */
TEST(cli_verbs_rights_must_match_the_engine)
{
    char *get = given_row_edit("get", "| read |", "| read-write |");
    char *purge = given_row_edit("purge", "| read-write |", "| read |");
    char *brain = given_row_edit("brain list", "| none | none |", "| none | read |");
    char *set = given_row_edit("settings set", "| read-write |", "| read |");
    char *bad = given_row_edit("version", "| none | none |", "| none | write |");
    char *planned = given_row_edit("dispatch", "| read-write |", "| write |");

    assert_mismatch(get, 1, "rights differ from the engine: `remember get`\n");
    assert_mismatch(purge, 1, "rights differ from the engine: `remember purge`\n");
    assert_mismatch(brain, 1, "rights differ from the engine: `remember brain list`\n");
    assert_mismatch(set, 1, "rights differ from the engine: `remember settings set`\n");
    assert_mismatch(bad, 2, "rights is not none, read or read-write: `remember version`\n");
    assert_mismatch(planned, 1, "rights is not none, read or read-write: `remember dispatch`\n");
    free(get);
    free(purge);
    free(brain);
    free(set);
    free(bad);
    free(planned);
}

/* A row the parser cannot read fails loudly instead of being skipped: a
   missing cell, a missing trailing bar, a command without its code span, not
   after (or not spelled) `remember `, two spaces between words, an open or an
   inner backtick, five words, three words on a main row, a line outside the table form, no Verbs
   section, and no spec at all. */
TEST(cli_verbs_malformed_rows_fail)
{
    char out[VT_REPORT_MAX];
    char *cells = given_row_edit("tags", "| BIN |", "|");
    char *pipe = given_row_edit("tags", "the ladder. |", "the ladder.");
    char *span = given_row_edit("tags", "`remember tags`", "remember tags");
    char *word = given_row_edit("tags", "`remember tags`", "`tags`");
    char *cap = given_row_edit("tags", "`remember tags`", "`Remember tags`");
    char *spaces = given_row_edit("tags", "`remember tags`", "`remember  tags`");
    char *open = given_row_edit("tags", "`remember tags`", "`remember tags");
    char *inner = given_row_edit("tags", "`remember tags`", "`remember ta`gs`");
    char *five = given_row_edit("tags", "`remember tags`", "`remember tags a b c d`");
    char *loose = given_row_edit("tags", "| `remember tags` |", " x | `remember tags` |");
    char *three = given_row_edit("brain list", "`remember brain list`", "`remember brain list x`");
    char *nosection = spec_edit(given_spec(), "\n## Verbs\n", "\n## Verbz\n");

    assert_mismatch(cells, 2, "row does not have 7 cells");
    assert_mismatch(pipe, 2, "row does not have 7 cells");
    assert_mismatch(span, 2, "row has no `remember ...` command");
    assert_mismatch(word, 2, "row has no `remember ...` command");
    assert_mismatch(cap, 2, "row has no `remember ...` command");
    assert_mismatch(spaces, 2, "row has no `remember ...` command");
    assert_mismatch(open, 2, "row has no `remember ...` command");
    assert_mismatch(inner, 2, "row has no `remember ...` command");
    assert_mismatch(five, 2, "row has no `remember ...` command");
    assert_mismatch(loose, 2, "line names a verb but is not a table row");
    assert_mismatch(three, 2, "plan main row has more than one subcommand");
    assert_mismatch(nosection, 1, "spec has no section: ## Verbs");
    ASSERT_EQ_INT(verb_table_mismatches(NULL, out, sizeof(out)), 1);
    free(cells);
    free(pipe);
    free(span);
    free(word);
    free(cap);
    free(spaces);
    free(open);
    free(inner);
    free(five);
    free(loose);
    free(three);
    free(nosection);
}

/* Only the Verbs section counts: a row in a later section is not read. */
TEST(cli_verbs_rows_outside_the_verbs_section_are_ignored)
{
    char out[VT_REPORT_MAX];
    char *spec =
        spec_edit(given_spec(), "\n## Not in this table\n",
                  "\n## Not in this table\n\n| `remember zzz` | none | read | - | - | main "
                  "| x |\n");

    ASSERT_TRUE(spec != NULL);
    ASSERT_EQ_INT(verb_table_mismatches(spec, out, sizeof(out)), 0);
    free(spec);
}

/* A planned subcommand under an existing noun (brain sweep, plan 32) does not
   count as the noun's main row: with every shipped brain row planned, `brain`
   has no main row. */
TEST(cli_verbs_planned_subcommand_does_not_cover_its_noun)
{
    char *spec = given_row_edit("brain new-id", "| main |", "| 26 |");

    spec = row_edit(spec, "brain list", "| main |", "| 26 |");
    spec = row_edit(spec, "brain prune", "| main |", "| 26 |");
    ASSERT_TRUE(spec != NULL && strstr(spec, "| `remember brain sweep` |") != NULL);
    assert_mismatch(spec, 4, "dispatch verb has no plan main row: brain\n");
    free(spec);
}

void register_cli_tests(void)
{
    RUN_TEST(cli_verbs_match_api_surface);
    RUN_TEST(cli_subcommand_table_lookup);
    RUN_TEST(cli_brain_class_follows_the_subcommand);
    RUN_TEST(cli_verbs_dispatch_verb_without_row_fails);
    RUN_TEST(cli_verbs_main_row_without_dispatch_verb_fails);
    RUN_TEST(cli_verbs_subcommand_without_row_fails);
    RUN_TEST(cli_verbs_rights_must_match_the_engine);
    RUN_TEST(cli_verbs_malformed_rows_fail);
    RUN_TEST(cli_verbs_rows_outside_the_verbs_section_are_ignored);
    RUN_TEST(cli_verbs_planned_subcommand_does_not_cover_its_noun);
}
