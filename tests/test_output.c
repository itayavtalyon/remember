#include "failing_stream.h"
#include "output.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Pure unit tests for output.c (plan 18b): argument guards, NULL fields, and
 * the rule sides the CLI suites do not reach. Expected text is written out by
 * hand from the JSON and human formats, not produced by the code under test.
 */

enum {
    SUBJECT_ID = 7,
    OTHER_ID = 8,
    NEIGHBOR_ID = 3,
    OTHER_NEIGHBOR_ID = 9,
    TAG_COUNT = 3,
    BAD_REASON = 99, /* outside StoreConflictReason */
    EURO_COUNT = 70, /* more three-byte scalars than the preview buffer holds */
    EURO_KEPT = 62,  /* how many of them fit before the cut */
    CONFLICT_FIELDS = 5,
    READ_CAP = 4096,
    STREAM_LIMIT = 5 /* "ab" then "cde" would fill it exactly */
};

static const char NOW[] = "2026-10-20T00:00:00.000Z";
static const char EURO[] = "\xe2\x82\xac"; /* U+20AC, three bytes */

static FILE *given_stream(void)
{
    FILE *f = tmpfile();

    if (f == NULL) {
        perror("tmpfile");
        abort();
    }
    return f;
}

/* Everything written to f equals want. Closes f. */
static void assert_written(FILE *f, const char *want)
{
    char got[READ_CAP];
    size_t n = 0;

    (void)fflush(f);
    (void)fseek(f, 0, SEEK_SET);
    n = fread(got, 1, sizeof got - 1U, f);
    got[n] = '\0';
    (void)fclose(f);
    ASSERT_STREQ(got, want);
}

/* n copies of U+20AC into buf, NUL-terminated. */
static void given_euros(char *buf, size_t n)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        memcpy(buf + (i * 3), EURO, 3);
    }
    buf[n * 3] = '\0';
}

static Entry given_entry(void)
{
    Entry e = {
        .id = SUBJECT_ID,
        .sync_id = (char *)"s-7",
        .key = (char *)"k:7",
        .body = (char *)"body",
        .source = (char *)"agent",
        .created_at = (char *)"c",
        .updated_at = (char *)"u",
        .version_vector = (char *)"{}",
    };
    return e;
}

static StoreNeighbor given_link(long long subject_id, long long from_id, StoreEdgeKind kind)
{
    StoreNeighbor n = {
        .subject_id = subject_id,
        .from_id = from_id,
        .to_id = (from_id == subject_id) ? NEIGHBOR_ID : subject_id,
        .neighbor_id = NEIGHBOR_ID,
        .neighbor_body = (char *)"nb",
        .neighbor_sync_id = (char *)"n-3",
        .kind = kind,
    };
    return n;
}

static StoreConflict given_conflict(void)
{
    StoreConflict c = {
        .id = 1,
        .sync_id = (char *)"s-1",
        .local_json = (char *)"{}",
        .incoming_json = (char *)"{}",
        .created_at = (char *)"c",
        .reason = STORE_CONFLICT_KEY_CLASH,
    };
    return c;
}

/*
 * Drive writer once per byte limit (B4). Unlimited, it succeeds and writes len
 * bytes. Every limit below len makes some write fail: the writer must return
 * -1 and stop there, so no later write is attempted.
 */
static void assert_reports_every_failed_write(int (*writer)(FILE *))
{
    FailingStream s;
    FILE *f = failing_stream_open(&s, FAILING_STREAM_CAP - 1U);
    size_t len = 0;
    size_t limit = 0;

    ASSERT_TRUE(f != NULL);
    if (f == NULL) {
        return;
    }
    ASSERT_EQ_INT(writer(f), 0);
    (void)fclose(f);
    len = s.len;
    ASSERT_TRUE(len > 0);
    for (limit = 0; limit < len; limit++) {
        int rc = 0;
        size_t late = 0;

        f = failing_stream_open(&s, limit);
        ASSERT_TRUE(f != NULL);
        if (f == NULL) {
            return;
        }
        rc = writer(f);
        /* Read before fclose: stdio may retry a failed byte when it closes. */
        late = s.late_writes;
        (void)fclose(f);
        ASSERT_EQ_INT(rc, -1);
        ASSERT_EQ_INT(late, 0);
        if (rc != -1 || late != 0) {
            (void)fprintf(stderr, "  at byte limit %zu of %zu\n", limit, len);
            return;
        }
    }
}

/* ---- entry JSON ----------------------------------------------------------- */

TEST(output_entry_json_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_entry_json(NULL, &e, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, NULL, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, &e, NULL), -1);
    assert_written(f, "");
}

TEST(output_entry_json_rejects_an_incomplete_entry)
{
    Entry no_sync_id = given_entry();
    Entry no_vv = given_entry();
    Entry vv_not_object = given_entry();
    FILE *f = given_stream();

    no_sync_id.sync_id = NULL;
    no_vv.version_vector = NULL;
    vv_not_object.version_vector = (char *)"[]";
    ASSERT_EQ_INT(output_entry_json(f, &no_sync_id, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, &no_vv, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, &vv_not_object, NOW), -1);
    assert_written(f, "");
}

TEST(output_entry_json_renders_a_null_source_as_unknown)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.source = NULL;
    ASSERT_EQ_INT(output_entry_json(f, &e, NOW), 0);
    assert_written(f, "{\"id\":7,\"sync_id\":\"s-7\",\"key\":\"k:7\",\"body\":\"body\",\"tags\":[],"
                      "\"source\":\"unknown\",\"created_at\":\"c\",\"updated_at\":\"u\","
                      "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                      "\"version_vector\":{}}");
}

TEST(output_entry_json_renders_null_text_fields_as_empty_strings)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.body = NULL;
    e.created_at = NULL;
    e.updated_at = NULL;
    ASSERT_EQ_INT(output_entry_json(f, &e, NOW), 0);
    assert_written(f, "{\"id\":7,\"sync_id\":\"s-7\",\"key\":\"k:7\",\"body\":\"\",\"tags\":[],"
                      "\"source\":\"agent\",\"created_at\":\"\",\"updated_at\":\"\","
                      "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                      "\"version_vector\":{}}");
}

/* ---- envelopes ------------------------------------------------------------ */

TEST(output_action_envelope_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_action_envelope(NULL, "created", &e, NOW), -1);
    ASSERT_EQ_INT(output_action_envelope(f, NULL, &e, NOW), -1);
    ASSERT_EQ_INT(output_action_envelope(f, "created", NULL, NOW), -1);
    assert_written(f, "");
}

TEST(output_deleted_list_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_deleted_list(NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_deleted_list(f, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_deleted_list_separates_entries_with_a_comma)
{
    Entry entries[] = {given_entry(), given_entry()};
    FILE *f = given_stream();

    entries[1].id = OTHER_ID;
    ASSERT_EQ_INT(output_deleted_list(f, entries, 2, NOW), 0);
    assert_written(f, "{\"version\":1,\"action\":\"deleted\",\"count\":2,\"entries\":["
                      "{\"id\":7,\"sync_id\":\"s-7\",\"key\":\"k:7\",\"body\":\"body\","
                      "\"tags\":[],\"source\":\"agent\",\"created_at\":\"c\",\"updated_at\":\"u\","
                      "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                      "\"version_vector\":{}},"
                      "{\"id\":8,\"sync_id\":\"s-7\",\"key\":\"k:7\",\"body\":\"body\","
                      "\"tags\":[],\"source\":\"agent\",\"created_at\":\"c\",\"updated_at\":\"u\","
                      "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                      "\"version_vector\":{}}]}\n");
}

TEST(output_get_envelope_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_get_envelope(NULL, &e, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_get_envelope(f, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_get_envelope(f, &e, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_get_envelope(f, &e, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_get_envelope_lists_only_the_subjects_links)
{
    Entry e = given_entry();
    StoreNeighbor links[] = {given_link(OTHER_ID, OTHER_ID, STORE_EDGE_RELATED),
                             given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED)};
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_get_envelope(f, &e, links, 2, NOW), 0);
    assert_written(f, "{\"version\":1,\"count\":1,\"entries\":[{\"id\":7,\"sync_id\":\"s-7\","
                      "\"key\":\"k:7\",\"body\":\"body\",\"tags\":[],\"source\":\"agent\","
                      "\"created_at\":\"c\",\"updated_at\":\"u\",\"expires_at\":null,"
                      "\"deleted_at\":null,\"bin\":\"live\",\"version_vector\":{},"
                      "\"links\":[{\"id\":3,\"sync_id\":\"n-3\",\"key\":null,\"type\":\"related\","
                      "\"bin\":\"live\",\"preview\":\"nb\"}]}]}\n");
}

TEST(output_list_envelope_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_list_envelope(NULL, 0, 1, 1, 1, &e, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_list_envelope(f, 0, 1, 1, 1, &e, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_list_envelope(f, 0, 1, 1, 1, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_list_envelope(f, 0, 1, 1, 1, &e, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_tags_envelope_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_tags_envelope(NULL, NULL, 0), -1);
    ASSERT_EQ_INT(output_tags_envelope(f, NULL, 1), -1);
    assert_written(f, "");
}

TEST(output_links_write_envelope_rejects_missing_arguments)
{
    StoreNeighbor link = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED);
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_links_write_envelope(NULL, "created", &link, 1, NOW), -1);
    ASSERT_EQ_INT(output_links_write_envelope(f, NULL, &link, 1, NOW), -1);
    ASSERT_EQ_INT(output_links_write_envelope(f, "created", &link, 1, NULL), -1);
    ASSERT_EQ_INT(output_links_write_envelope(f, "created", NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_links_write_envelope_rejects_a_link_without_a_sync_id)
{
    StoreNeighbor link = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED);
    FILE *f = given_stream();

    link.neighbor_sync_id = NULL;
    ASSERT_EQ_INT(output_links_write_envelope(f, "created", &link, 1, NOW), -1);
    (void)fclose(f);
}

TEST(output_links_write_envelope_names_both_supersedes_directions)
{
    StoreNeighbor links[] = {given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_SUPERSEDES),
                             given_link(SUBJECT_ID, NEIGHBOR_ID, STORE_EDGE_SUPERSEDES)};
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_links_write_envelope(f, "created", links, 2, NOW), 0);
    assert_written(f, "{\"version\":1,\"action\":\"created\",\"count\":2,\"links\":["
                      "{\"id\":3,\"sync_id\":\"n-3\",\"key\":null,\"type\":\"supersedes\","
                      "\"bin\":\"live\",\"preview\":\"nb\"},"
                      "{\"id\":3,\"sync_id\":\"n-3\",\"key\":null,\"type\":\"superseded_by\","
                      "\"bin\":\"live\",\"preview\":\"nb\"}]}\n");
}

TEST(output_related_envelope_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_related_envelope(NULL, 1, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_related_envelope(f, 1, NULL, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_related_envelope(f, 1, NULL, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_import_envelope_rejects_missing_arguments)
{
    StoreImportCounts counts = {0};
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_import_envelope(NULL, &counts), -1);
    ASSERT_EQ_INT(output_import_envelope(f, NULL), -1);
    assert_written(f, "");
}

TEST(output_device_envelopes_reject_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_device_envelope(NULL, "d"), -1);
    ASSERT_EQ_INT(output_device_envelope(f, NULL), -1);
    ASSERT_EQ_INT(output_device_new_envelope(NULL, "d", NULL), -1);
    ASSERT_EQ_INT(output_device_new_envelope(f, NULL, NULL), -1);
    assert_written(f, "");
}

TEST(output_conflicts_envelope_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_conflicts_envelope(NULL, NULL, 0), -1);
    ASSERT_EQ_INT(output_conflicts_envelope(f, NULL, 1), -1);
    assert_written(f, "");
}

TEST(output_conflicts_envelope_lists_no_conflicts)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_conflicts_envelope(f, NULL, 0), 0);
    assert_written(f, "{\"version\":1,\"count\":0,\"conflicts\":[]}\n");
}

TEST(output_conflicts_envelope_separates_conflicts_with_a_comma)
{
    StoreConflict rows[] = {given_conflict(), given_conflict()};
    FILE *f = given_stream();

    rows[1].id = 2;
    ASSERT_EQ_INT(output_conflicts_envelope(f, rows, 2), 0);
    assert_written(f, "{\"version\":1,\"count\":2,\"conflicts\":["
                      "{\"id\":1,\"sync_id\":\"s-1\",\"reason\":\"key_clash\",\"local\":{},"
                      "\"incoming\":{},\"created_at\":\"c\"},"
                      "{\"id\":2,\"sync_id\":\"s-1\",\"reason\":\"key_clash\",\"local\":{},"
                      "\"incoming\":{},\"created_at\":\"c\"}]}\n");
}

/* Each field the conflict row needs, missing in turn. The envelope header is
   already written when the row fails, so only the status is the contract. */
TEST(output_conflicts_envelope_rejects_an_incomplete_conflict)
{
    int bad_reason = BAD_REASON;
    StoreConflict rows[CONFLICT_FIELDS] = {given_conflict(), given_conflict(), given_conflict(),
                                           given_conflict(), given_conflict()};
    FILE *f = given_stream();

    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the guard under test
    rows[0].reason = (StoreConflictReason)bad_reason;
    rows[1].sync_id = NULL;
    rows[2].local_json = NULL;
    rows[3].incoming_json = NULL;
    rows[4].created_at = NULL;
    ASSERT_EQ_INT(output_conflicts_envelope(f, &rows[0], 1), -1);
    ASSERT_EQ_INT(output_conflicts_envelope(f, &rows[1], 1), -1);
    ASSERT_EQ_INT(output_conflicts_envelope(f, &rows[2], 1), -1);
    ASSERT_EQ_INT(output_conflicts_envelope(f, &rows[3], 1), -1);
    ASSERT_EQ_INT(output_conflicts_envelope(f, &rows[4], 1), -1);
    (void)fclose(f);
}

TEST(output_accepted_envelope_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_accepted_envelope(NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_accepted_envelope(f, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_accepted_envelope(f, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_accepted_envelope_lists_no_entries)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_accepted_envelope(f, NULL, 0, NOW), 0);
    assert_written(f, "{\"version\":1,\"action\":\"accepted\",\"count\":0,\"entries\":[]}\n");
}

/* ---- human ---------------------------------------------------------------- */

TEST(output_human_writers_reject_a_missing_stream)
{
    ASSERT_EQ_INT(output_id_human(NULL, 1), -1);
    ASSERT_EQ_INT(output_body_human(NULL, "b"), -1);
    ASSERT_EQ_INT(output_device_human(NULL, "d"), -1);
    ASSERT_EQ_INT(output_rebuilt(NULL, false, 0), -1);
}

TEST(output_device_human_rejects_a_missing_id)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_device_human(f, NULL), -1);
    assert_written(f, "");
}

TEST(output_tags_human_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_tags_human(NULL, NULL, 0), -1);
    ASSERT_EQ_INT(output_tags_human(f, NULL, 1), -1);
    assert_written(f, "");
}

TEST(output_tags_human_writes_nothing_for_no_tags)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_tags_human(f, NULL, 0), 0);
    assert_written(f, "");
}

TEST(output_tags_human_renders_a_null_name_as_empty)
{
    TagCount tag = {.name = NULL, .count = TAG_COUNT};
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_tags_human(f, &tag, 1), 0);
    assert_written(f, "\t3\n");
}

TEST(output_entry_human_line_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_entry_human_line(NULL, &e, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_entry_human_line(f, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_entry_human_line_skips_a_null_tag)
{
    char *tags[] = {(char *)"a", NULL, (char *)"b"};
    Entry e = given_entry();
    FILE *f = given_stream();

    e.tags = tags;
    e.ntags = 3;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, "7 | k:7 | a,,b | body | u | \n");
}

TEST(output_entry_human_line_leaves_a_null_updated_at_empty)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.updated_at = NULL;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, "7 | k:7 |  | body |  | \n");
}

TEST(output_entry_human_line_leaves_a_null_body_preview_empty)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.body = NULL;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, "7 | k:7 |  |  | u | \n");
}

/* Five links of the subject fill the cell exactly (no ", +N"); the other
   subject's link, listed first, must not be shown or counted. */
TEST(output_entry_human_line_counts_only_the_subjects_links)
{
    Entry e = given_entry();
    StoreNeighbor links[] = {
        given_link(OTHER_ID, OTHER_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
    };
    FILE *f = given_stream();

    links[0].neighbor_id = OTHER_NEIGHBOR_ID;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, links, sizeof links / sizeof links[0], NOW), 0);
    assert_written(f, "7 | k:7 |  | body | u | 3, 3, 3, 3, 3\n");
}

TEST(output_preview_stops_at_a_carriage_return)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.body = (char *)"ab\rcd";
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, "7 | k:7 |  | ab | u | \n");
}

TEST(output_preview_masks_del)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.body = (char *)"a\x7f"
                     "b";
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, "7 | k:7 |  | a?b | u | \n");
}

/* 192-byte buffer: the 63rd three-byte scalar would leave no room for "..." and
   the NUL (62 * 3 + 3 + 4 >= 192), so the cut is by bytes, not the 80-scalar cap. */
TEST(output_preview_cuts_by_bytes_before_the_scalar_cap)
{
    char body[(EURO_COUNT * 3) + 1];
    char cut[(EURO_KEPT * 3) + 1];
    char want[READ_CAP];
    Entry e = given_entry();
    FILE *f = given_stream();

    given_euros(body, EURO_COUNT);
    given_euros(cut, EURO_KEPT);
    (void)snprintf(want, sizeof want, "7 | k:7 |  | %s... | u | \n", cut);
    e.body = body;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, want);
}

TEST(output_related_human_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_related_human(NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_related_human(f, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_related_human(f, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_related_human_omits_an_empty_preview)
{
    StoreNeighbor link = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED);
    FILE *f = given_stream();

    link.neighbor_body = NULL;
    ASSERT_EQ_INT(output_related_human(f, &link, 1, NOW), 0);
    assert_written(f, "Related:\nrelated 3\n");
}

/* ---- failed writes (B4) --------------------------------------------------- */

static Entry given_full_entry(void)
{
    static char *tags[] = {(char *)"a", (char *)"b"};
    Entry e = given_entry();

    e.tags = tags;
    e.ntags = 2;
    return e;
}

/* Two links of the subject: one keyed and deleted, one keyless and live. */
static void given_two_links(StoreNeighbor links[2])
{
    links[0] = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_CITES);
    links[0].neighbor_key = (char *)"k:3";
    links[0].neighbor_deleted_at = (char *)"d";
    links[1] = given_link(SUBJECT_ID, NEIGHBOR_ID, STORE_EDGE_RELATED);
}

static int write_json_string(FILE *f)
{
    return output_json_string(f, "q\"b\\\b\f\n\r\t\x01z");
}

static int write_entry_json(FILE *f)
{
    Entry e = given_full_entry();

    return output_entry_json(f, &e, NOW);
}

static int write_action_envelope(FILE *f)
{
    Entry e = given_full_entry();

    return output_action_envelope(f, "created", &e, NOW);
}

static int write_deleted_list(FILE *f)
{
    Entry entries[] = {given_full_entry(), given_full_entry()};

    return output_deleted_list(f, entries, 2, NOW);
}

static int write_get_envelope(FILE *f)
{
    Entry e = given_full_entry();
    StoreNeighbor links[2];

    given_two_links(links);
    return output_get_envelope(f, &e, links, 2, NOW);
}

static int write_list_envelope(FILE *f)
{
    Entry entries[] = {given_full_entry(), given_full_entry()};
    StoreNeighbor links[2];

    given_two_links(links);
    return output_list_envelope(f, 0, 2, 2, 2, entries, links, 2, NOW);
}

static int write_id_human(FILE *f)
{
    return output_id_human(f, SUBJECT_ID);
}

static int write_text_human(FILE *f)
{
    return output_text_human(f,
                             "a\x01"
                             "b",
                             false);
}

static int write_body_human(FILE *f)
{
    return output_body_human(f, "body");
}

static int write_tags_envelope(FILE *f)
{
    TagCount tags[] = {{.name = (char *)"a", .count = 1}, {.name = (char *)"b", .count = 2}};

    return output_tags_envelope(f, tags, 2);
}

static int write_tags_human(FILE *f)
{
    TagCount tags[] = {{.name = (char *)"a", .count = 1}, {.name = (char *)"b", .count = 2}};

    return output_tags_human(f, tags, 2);
}

static int write_links_write_envelope(FILE *f)
{
    StoreNeighbor links[2];

    given_two_links(links);
    return output_links_write_envelope(f, "created", links, 2, NOW);
}

static int write_related_envelope_with_key(FILE *f)
{
    StoreNeighbor links[2];

    given_two_links(links);
    return output_related_envelope(f, SUBJECT_ID, "k:7", links, 2, NOW);
}

static int write_related_envelope_without_key(FILE *f)
{
    return output_related_envelope(f, SUBJECT_ID, NULL, NULL, 0, NOW);
}

static int write_related_human(FILE *f)
{
    StoreNeighbor links[2];

    given_two_links(links);
    return output_related_human(f, links, 2, NOW);
}

static int write_import_envelope(FILE *f)
{
    StoreImportCounts counts = {.inserted = 1, .updated = 2, .unchanged = 3, .conflicts = 4};

    return output_import_envelope(f, &counts);
}

static int write_device_human(FILE *f)
{
    return output_device_human(f, "dev");
}

static int write_device_envelope(FILE *f)
{
    return output_device_envelope(f, "dev");
}

static int write_device_new_envelope_with_previous(FILE *f)
{
    return output_device_new_envelope(f, "dev", "old");
}

static int write_device_new_envelope_without_previous(FILE *f)
{
    return output_device_new_envelope(f, "dev", NULL);
}

static int write_conflicts_envelope(FILE *f)
{
    StoreConflict rows[] = {given_conflict(), given_conflict()};

    return output_conflicts_envelope(f, rows, 2);
}

static int write_accepted_envelope(FILE *f)
{
    Entry entries[] = {given_full_entry(), given_full_entry()};

    return output_accepted_envelope(f, entries, 2, NOW);
}

static int write_rebuilt_json(FILE *f)
{
    return output_rebuilt(f, true, 2);
}

static int write_rebuilt_human(FILE *f)
{
    return output_rebuilt(f, false, 2);
}

/* The driver above leans on two helper rules: a write that reaches the limit
   exactly still succeeds, and after one failure even a write that would fit
   fails and is counted. */
TEST(failing_stream_fails_past_the_limit_and_stays_failed)
{
    FailingStream s;
    FILE *f = failing_stream_open(&s, STREAM_LIMIT);

    ASSERT_TRUE(f != NULL);
    if (f == NULL) {
        return;
    }
    ASSERT_TRUE(fputs("ab", f) >= 0);
    ASSERT_EQ_INT(fputs("cdef", f), EOF);
    ASSERT_EQ_INT(s.late_writes, 0);
    ASSERT_EQ_INT(fputs("cde", f), EOF);
    ASSERT_EQ_INT(s.late_writes, 1);
    ASSERT_STREQ(s.buf, "ab");
    (void)fclose(f);

    f = failing_stream_open(&s, 2);
    ASSERT_TRUE(f != NULL);
    if (f == NULL) {
        return;
    }
    ASSERT_TRUE(fputs("ab", f) >= 0);
    ASSERT_STREQ(s.buf, "ab");
    (void)fclose(f);
}

TEST(output_json_string_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_json_string);
}

TEST(output_entry_json_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_entry_json);
}

TEST(output_action_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_action_envelope);
}

TEST(output_deleted_list_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_deleted_list);
}

TEST(output_get_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_get_envelope);
}

TEST(output_list_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_list_envelope);
}

TEST(output_id_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_id_human);
}

TEST(output_text_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_text_human);
}

TEST(output_body_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_body_human);
}

TEST(output_tags_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_tags_envelope);
}

TEST(output_tags_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_tags_human);
}

TEST(output_links_write_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_links_write_envelope);
}

TEST(output_related_envelope_with_key_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_related_envelope_with_key);
}

TEST(output_related_envelope_without_key_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_related_envelope_without_key);
}

TEST(output_related_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_related_human);
}

TEST(output_import_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_import_envelope);
}

TEST(output_device_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_device_human);
}

TEST(output_device_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_device_envelope);
}

TEST(output_device_new_envelope_with_previous_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_device_new_envelope_with_previous);
}

TEST(output_device_new_envelope_without_previous_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_device_new_envelope_without_previous);
}

TEST(output_conflicts_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_conflicts_envelope);
}

TEST(output_accepted_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_accepted_envelope);
}

TEST(output_rebuilt_json_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_rebuilt_json);
}

TEST(output_rebuilt_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_rebuilt_human);
}

static int write_entry_human_line(FILE *f)
{
    char *tags[] = {(char *)"a", (char *)"b"};
    Entry e = given_entry();
    StoreNeighbor links[] = {
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
        given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED),
    };

    links[0].neighbor_deleted_at = (char *)"d";
    e.tags = tags;
    e.ntags = 2;
    return output_entry_human_line(f, &e, links, sizeof links / sizeof links[0], NOW);
}

TEST(output_entry_human_line_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_entry_human_line);
}

void register_output_tests(void)
{
    RUN_TEST(output_entry_json_rejects_missing_arguments);
    RUN_TEST(output_entry_json_rejects_an_incomplete_entry);
    RUN_TEST(output_entry_json_renders_a_null_source_as_unknown);
    RUN_TEST(output_entry_json_renders_null_text_fields_as_empty_strings);
    RUN_TEST(output_action_envelope_rejects_missing_arguments);
    RUN_TEST(output_deleted_list_rejects_missing_arguments);
    RUN_TEST(output_deleted_list_separates_entries_with_a_comma);
    RUN_TEST(output_get_envelope_rejects_missing_arguments);
    RUN_TEST(output_get_envelope_lists_only_the_subjects_links);
    RUN_TEST(output_list_envelope_rejects_missing_arguments);
    RUN_TEST(output_tags_envelope_rejects_missing_arguments);
    RUN_TEST(output_links_write_envelope_rejects_missing_arguments);
    RUN_TEST(output_links_write_envelope_rejects_a_link_without_a_sync_id);
    RUN_TEST(output_links_write_envelope_names_both_supersedes_directions);
    RUN_TEST(output_related_envelope_rejects_missing_arguments);
    RUN_TEST(output_import_envelope_rejects_missing_arguments);
    RUN_TEST(output_device_envelopes_reject_missing_arguments);
    RUN_TEST(output_conflicts_envelope_rejects_missing_arguments);
    RUN_TEST(output_conflicts_envelope_lists_no_conflicts);
    RUN_TEST(output_conflicts_envelope_separates_conflicts_with_a_comma);
    RUN_TEST(output_conflicts_envelope_rejects_an_incomplete_conflict);
    RUN_TEST(output_accepted_envelope_rejects_missing_arguments);
    RUN_TEST(output_accepted_envelope_lists_no_entries);
    RUN_TEST(output_human_writers_reject_a_missing_stream);
    RUN_TEST(output_device_human_rejects_a_missing_id);
    RUN_TEST(output_tags_human_rejects_missing_arguments);
    RUN_TEST(output_tags_human_writes_nothing_for_no_tags);
    RUN_TEST(output_tags_human_renders_a_null_name_as_empty);
    RUN_TEST(output_entry_human_line_rejects_missing_arguments);
    RUN_TEST(output_entry_human_line_skips_a_null_tag);
    RUN_TEST(output_entry_human_line_leaves_a_null_updated_at_empty);
    RUN_TEST(output_entry_human_line_leaves_a_null_body_preview_empty);
    RUN_TEST(output_entry_human_line_counts_only_the_subjects_links);
    RUN_TEST(output_preview_stops_at_a_carriage_return);
    RUN_TEST(output_preview_masks_del);
    RUN_TEST(output_preview_cuts_by_bytes_before_the_scalar_cap);
    RUN_TEST(output_related_human_rejects_missing_arguments);
    RUN_TEST(output_related_human_omits_an_empty_preview);
    RUN_TEST(failing_stream_fails_past_the_limit_and_stays_failed);
    RUN_TEST(output_json_string_reports_every_failed_write);
    RUN_TEST(output_entry_json_reports_every_failed_write);
    RUN_TEST(output_action_envelope_reports_every_failed_write);
    RUN_TEST(output_deleted_list_reports_every_failed_write);
    RUN_TEST(output_get_envelope_reports_every_failed_write);
    RUN_TEST(output_list_envelope_reports_every_failed_write);
    RUN_TEST(output_id_human_reports_every_failed_write);
    RUN_TEST(output_text_human_reports_every_failed_write);
    RUN_TEST(output_body_human_reports_every_failed_write);
    RUN_TEST(output_tags_envelope_reports_every_failed_write);
    RUN_TEST(output_tags_human_reports_every_failed_write);
    RUN_TEST(output_links_write_envelope_reports_every_failed_write);
    RUN_TEST(output_related_envelope_with_key_reports_every_failed_write);
    RUN_TEST(output_related_envelope_without_key_reports_every_failed_write);
    RUN_TEST(output_related_human_reports_every_failed_write);
    RUN_TEST(output_import_envelope_reports_every_failed_write);
    RUN_TEST(output_device_human_reports_every_failed_write);
    RUN_TEST(output_device_envelope_reports_every_failed_write);
    RUN_TEST(output_device_new_envelope_with_previous_reports_every_failed_write);
    RUN_TEST(output_device_new_envelope_without_previous_reports_every_failed_write);
    RUN_TEST(output_conflicts_envelope_reports_every_failed_write);
    RUN_TEST(output_accepted_envelope_reports_every_failed_write);
    RUN_TEST(output_rebuilt_json_reports_every_failed_write);
    RUN_TEST(output_rebuilt_human_reports_every_failed_write);
    RUN_TEST(output_entry_human_line_reports_every_failed_write);
}
