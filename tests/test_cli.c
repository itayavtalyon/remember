#include "cli.h"
#include "commands.h"
#include "harness.h"
#include "register.h"
#include "remember_app.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The engine verb table (decision:2026-11-18-verb-ladder): every top-level verb
   in the CLI dispatch table has a row with plan `main` in specs/API-SURFACE.md,
   every such row names a verb the dispatch table has, and its Rights match the
   engine. ponytail: subcommands are checked through their rights only; the
   CLI has no subcommand table to compare them with (plan 29's api_table.c adds
   one). */

enum {
    VT_CELLS = 7,         /* columns of the Verbs table */
    VT_CELL_MAX = 512,    /* one cell, unescaped */
    VT_MAX_WORDS = 4,     /* `remember a b c` */
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
    int n = snprintf(r->buf + r->len, r->cap - r->len, "%s: %s\n", what, subject);

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

/* "`remember brain list`" -> words {"brain", "list"}. Returns the word count,
   or 0 when the cell is not one code span that starts with `remember `. */
static int command_words(char *cell, const char *words[VT_MAX_WORDS])
{
    static const char k_prefix[] = "`remember ";
    size_t len = strlen(cell);
    int n = 0;
    char *p = NULL;

    if (len < sizeof(k_prefix) || strncmp(cell, k_prefix, sizeof(k_prefix) - 1) != 0 ||
        cell[len - 1] != '`') {
        return 0;
    }
    cell[len - 1] = '\0';
    p = cell + sizeof(k_prefix) - 1;
    while (*p != '\0') {
        if (*p == ' ' || *p == '`' || n == VT_MAX_WORDS) {
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
static const char *engine_rights(CliCommand cmd, int argc, const char **argv)
{
    if (cmd == CLI_CMD_BRAIN && !cmd_brain_uses_store(argc, argv)) {
        return "none";
    }
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

static void check_row(char cells[][VT_CELL_MAX], const char *line, bool seen[], Report *r)
{
    const char *words[VT_MAX_WORDS] = {NULL};
    const char *rights = cells[VT_COL_RIGHTS];
    char name[VT_CELL_MAX];
    int n = 0;
    CliCommand cmd = CLI_CMD_UNKNOWN;

    (void)snprintf(name, sizeof(name), "%s", cells[VT_COL_COMMAND]);
    n = command_words(cells[VT_COL_COMMAND], words);

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
    seen[cmd] = true;
    if (strcmp(engine_rights(cmd, n - 1, words + 1), rights) != 0) {
        report_add(r, "rights differ from the engine", name);
    }
}

/* Every mismatch between the dispatch table and the `## Verbs` table of spec,
   one line each in out; returns their count. */
static int verb_table_mismatches(const char *spec, char *out, size_t outlen)
{
    char cells[VT_CELLS + 1][VT_CELL_MAX];
    bool seen[CLI_CMD_UNKNOWN + 1] = {false};
    Report r = {.buf = out, .cap = outlen, .len = 0, .count = 0, .pad_ = {0}};
    const char *p = strstr(spec, "\n## Verbs\n");
    const char *end = NULL;
    int cmd = 0;

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
                check_row(cells, line, seen, &r);
            }
        } else if (len > 0 && strchr(line, '|') != NULL && strstr(line, "remember ") != NULL) {
            report_add(&r, "line names a verb but is not a table row", line);
        }
        p += len + 1;
    }
    for (cmd = CLI_CMD_NONE + 1; cmd < CLI_CMD_UNKNOWN; cmd++) {
        if (!seen[cmd]) {
            report_add(&r, "dispatch verb has no plan main row", cli_command_name((CliCommand)cmd));
        }
    }
    return r.count;
}

/* The real spec, with every row of the real table, as the fixtures' base. */
static char *given_spec(void)
{
    size_t len = 0;
    char *spec = harness_read_file(REMEMBER_SOURCE_DIR "/specs/API-SURFACE.md", &len);

    ASSERT_TRUE(spec != NULL);
    return spec;
}

/* spec with the first `from` replaced by `to` (heap). Takes spec. */
static char *spec_edit(char *spec, const char *from, const char *to)
{
    char *at = spec != NULL ? strstr(spec, from) : NULL;
    size_t len = 0;
    char *out = NULL;

    ASSERT_TRUE(at != NULL);
    if (at == NULL) {
        return spec;
    }
    len = strlen(spec) - strlen(from) + strlen(to) + 1;
    out = (char *)malloc(len);
    ASSERT_TRUE(out != NULL);
    if (out != NULL) {
        (void)snprintf(out, len, "%.*s%s%s", (int)(at - spec), spec, to, at + strlen(from));
    }
    free(spec);
    return out;
}

static char *given_spec_edited(const char *from, const char *to)
{
    return spec_edit(given_spec(), from, to);
}

static void assert_mismatch(const char *spec, int want, const char *needle)
{
    char out[VT_REPORT_MAX];

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

/* A verb in the dispatch table with no row: drop the `tags` row. */
TEST(cli_verbs_dispatch_verb_without_row_fails)
{
    char *spec = given_spec_edited("| `remember tags` |", "| `remember tagz` |");

    assert_mismatch(spec, 2, "dispatch verb has no plan main row: tags\n");
    ASSERT_STR_CONTAINS(spec, "tagz");
    free(spec);
}

/* A shipped row the dispatch table lacks; a planned row for it passes. */
TEST(cli_verbs_main_row_without_dispatch_verb_fails)
{
    char *main_row =
        given_spec_edited("| none | read | - | - | 44 |", "| none | read | - | - | main |");

    assert_mismatch(main_row, 1, "plan main row names a verb the dispatch table lacks: sync\n");
    free(main_row);
}

/* Rights: a read marked read-write, a write marked read, a no-brain verb
   marked read, and a settings subcommand that writes marked read. */
TEST(cli_verbs_rights_must_match_the_engine)
{
    char *get = given_spec_edited("| LOC, BIN | read |", "| LOC, BIN | read-write |");
    char *purge =
        given_spec_edited("HOOKS | read-write | - | - | main | On main before the ladder. "
                          "Plan 32",
                          "HOOKS | read | - | - | main | On main before the ladder. "
                          "Plan 32");
    char *brain = given_spec_edited("| `remember brain list` | none | none |",
                                    "| `remember brain list` | none | read |");
    char *set = given_spec_edited("`--scope`, AUTHOR | read-write |", "`--scope`, AUTHOR | read |");
    char *bad = given_spec_edited("| `remember version` | none | none |",
                                  "| `remember version` | none | write |");

    assert_mismatch(get, 1, "rights differ from the engine: `remember get`\n");
    assert_mismatch(purge, 1, "rights differ from the engine: `remember purge`\n");
    assert_mismatch(brain, 1, "rights differ from the engine: `remember brain list`\n");
    assert_mismatch(set, 1, "rights differ from the engine: `remember settings set`\n");
    assert_mismatch(bad, 2, "rights is not none, read or read-write: `remember version`\n");
    free(get);
    free(purge);
    free(brain);
    free(set);
    free(bad);
}

/* A row the parser cannot read fails loudly instead of being skipped: a
   missing cell, a command without its code span, a command not after (or
   not spelled) `remember `, two spaces between words, a line that names a
   verb outside the table form, and a spec with no Verbs section. */
TEST(cli_verbs_malformed_rows_fail)
{
    char *cells = given_spec_edited("| `remember tags` | BIN |", "| `remember tags` |");
    char *span = given_spec_edited("| `remember tags` |", "| remember tags |");
    char *word = given_spec_edited("| `remember tags` |", "| `tags` |");
    char *cap = given_spec_edited("| `remember tags` |", "| `Remember tags` |");
    char *spaces = given_spec_edited("| `remember tags` |", "| `remember  tags` |");
    char *noindent = given_spec_edited("\n| `remember tags` |", "\n x | `remember tags` |");
    char *nosection = given_spec_edited("\n## Verbs\n", "\n## Verb list\n");

    assert_mismatch(cells, 2, "row does not have 7 cells");
    assert_mismatch(span, 2, "row has no `remember ...` command");
    assert_mismatch(word, 2, "row has no `remember ...` command");
    assert_mismatch(cap, 2, "row has no `remember ...` command");
    assert_mismatch(spaces, 2, "row has no `remember ...` command");
    assert_mismatch(noindent, 2, "line names a verb but is not a table row");
    assert_mismatch(nosection, 1, "spec has no section: ## Verbs");
    free(cells);
    free(span);
    free(word);
    free(cap);
    free(spaces);
    free(noindent);
    free(nosection);
}

/* A planned row under an existing noun (brain sweep, plan 32) does not
   count as the noun's main row: with every shipped brain row marked planned,
   `brain` has no main row. */
/* Only the Verbs section counts: a row in a later section is not read. */
TEST(cli_verbs_rows_outside_the_verbs_section_are_ignored)
{
    char out[VT_REPORT_MAX];
    char *spec = given_spec_edited("\n## Not in this table\n",
                                   "\n## Not in this table\n\n| `remember zzz` | none | read | - "
                                   "| - | main | x |\n");

    ASSERT_EQ_INT(verb_table_mismatches(spec, out, sizeof(out)), 0);
    free(spec);
}

TEST(cli_verbs_planned_subcommand_does_not_cover_its_noun)
{
    char *spec = given_spec_edited("| `remember brain new-id` | none | read-write | - | - | main |",
                                   "| `remember brain new-id` | none | read-write | - | - | 26 |");

    spec = spec_edit(spec, "| `remember brain list` | none | none | - | - | main |",
                     "| `remember brain list` | none | none | - | - | 26 |");
    spec = spec_edit(spec, "| `remember brain prune` | none | none | - | - | main |",
                     "| `remember brain prune` | none | none | - | - | 26 |");
    ASSERT_STR_CONTAINS(spec, "| `remember brain sweep` |");
    assert_mismatch(spec, 1, "dispatch verb has no plan main row: brain\n");
    free(spec);
}

void register_cli_tests(void)
{
    RUN_TEST(cli_verbs_match_api_surface);
    RUN_TEST(cli_verbs_dispatch_verb_without_row_fails);
    RUN_TEST(cli_verbs_main_row_without_dispatch_verb_fails);
    RUN_TEST(cli_verbs_rights_must_match_the_engine);
    RUN_TEST(cli_verbs_malformed_rows_fail);
    RUN_TEST(cli_verbs_rows_outside_the_verbs_section_are_ignored);
    RUN_TEST(cli_verbs_planned_subcommand_does_not_cover_its_noun);
}
