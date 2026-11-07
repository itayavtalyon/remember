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
    STREAM_LIMIT = 5, /* "ab" then "cde" would fill it exactly */
    KEY_MATCHES = 5,  /* keys total in given_search_facets */
    MAX_KEY_ITEMS = 12,
    HUMAN_CAP = 10, /* E2.7: items one human facet line shows */
    MANY_MATCHES = 60
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
        .created_by = (char *)"{\"kind\":\"agent\",\"id\":null}",
        .updated_by = (char *)"{\"kind\":\"agent\",\"id\":null}",
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

/* The vector is written raw: only the canonical form may pass
   (decision:plan18c-validate-peer-fields). */
TEST(output_entry_json_rejects_a_non_canonical_version_vector)
{
    Entry injected = given_entry();
    Entry not_uuid = given_entry();
    FILE *f = given_stream();

    injected.version_vector = (char *)"{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1},\"pwn\":true";
    not_uuid.version_vector = (char *)"{\"a\":1}";
    ASSERT_EQ_INT(output_entry_json(f, &injected, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, &not_uuid, NOW), -1);
    assert_written(f, "");
}

TEST(output_entry_json_rejects_a_bad_actor)
{
    Entry missing = given_entry();
    Entry bad_created = given_entry();
    Entry missing_updated = given_entry();
    Entry bad_updated = given_entry();
    FILE *f = given_stream();

    missing.created_by = NULL;
    bad_created.created_by = (char *)"human";
    missing_updated.updated_by = NULL;
    bad_updated.updated_by = (char *)"{\"kind\":\"human\"}";
    ASSERT_EQ_INT(output_entry_json(f, &missing, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, &bad_created, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, &missing_updated, NOW), -1);
    ASSERT_EQ_INT(output_entry_json(f, &bad_updated, NOW), -1);
    assert_written(f, "");
}

TEST(output_entry_json_renders_null_text_fields_as_empty_strings)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.body = NULL;
    e.created_at = NULL;
    e.updated_at = NULL;
    ASSERT_EQ_INT(output_entry_json(f, &e, NOW), 0);
    assert_written(f, "{\"id\":\"s-7\",\"local_id\":7,\"key\":\"k:7\",\"body\":\"\",\"tags\":[],"
                      "\"created_by\":{\"kind\":\"agent\",\"id\":null},"
                      "\"updated_by\":{\"kind\":\"agent\",\"id\":null},"
                      "\"created_at\":\"\",\"updated_at\":\"\","
                      "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                      "\"version_vector\":{}}");
}

/* ---- envelopes ------------------------------------------------------------ */

TEST(output_action_envelope_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_action_data(NULL, "updated", &e, NOW), -1);
    ASSERT_EQ_INT(output_action_data(f, NULL, &e, NOW), -1);
    ASSERT_EQ_INT(output_action_data(f, "updated", NULL, NOW), -1);
    assert_written(f, "");
}

TEST(output_entries_envelope_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_entries_data(NULL, "deleted", NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_entries_data(f, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_entries_data(f, "deleted", NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_entries_envelope_separates_entries_with_a_comma)
{
    Entry entries[] = {given_entry(), given_entry()};
    FILE *f = given_stream();

    entries[1].id = OTHER_ID;
    ASSERT_EQ_INT(output_entries_data(f, "deleted", entries, 2, NOW), 0);
    assert_written(
        f, "{\"action\":\"deleted\",\"count\":2,\"entries\":[{\"id\":\"s-7\",\"local_id\":7,\"key"
           "\":\"k:7\",\"body\":\"body\",\"tags\":[],\"created_by\":{\"kind\":\"agent\",\"id\":"
           "null},\"updated_by\":{\"kind\":\"agent\",\"id\":null},\"created_at\":\"c\","
           "\"updated_at\":\"u\",\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\",\"vers"
           "ion_vector\":{}},{\"id\":\"s-7\",\"local_id\":8,\"key\":\"k:7\",\"body\":\"body\",\"t"
           "ags\":[],\"created_by\":{\"kind\":\"agent\",\"id\":null},\"updated_by\":{\"kind\":"
           "\"agent\",\"id\":null},\"created_at\":\"c\",\"updated_at\":\"u\",\"expires_at"
           "\":null,\"deleted_at\":null,\"bin\":\"live\",\"version_vector\":{}}]}");
}

TEST(output_memory_envelope_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_memory_data(NULL, NULL, &e, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_memory_data(f, NULL, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_memory_data(f, NULL, &e, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_memory_data(f, NULL, &e, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_memory_envelope_without_action_lists_only_the_subjects_links)
{
    Entry e = given_entry();
    StoreNeighbor links[] = {given_link(OTHER_ID, OTHER_ID, STORE_EDGE_RELATED),
                             given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED)};
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_memory_data(f, NULL, &e, links, 2, NOW), 0);
    assert_written(
        f, "{\"memory\":{\"id\":\"s-7\",\"local_id\":7,\"key\":\"k:7\",\"body\":\"body\",\"tags\""
           ":[],\"created_by\":{\"kind\":\"agent\",\"id\":null},\"updated_by\":{\"kind\":\"agent\","
           "\"id\":null},\"created_at\":\"c\",\"updated_at\":\"u\",\"expires_at\":nu"
           "ll,\"deleted_at\":null,\"bin\":\"live\",\"version_vector\":{},\"links\":[{\"id\":\"n-"
           "3\",\"local_id\":3,"
           "\"key\":null,\"type\":\"related\",\"bin\":\"live\",\"preview\":"
           "\"nb\"}]}}");
}

TEST(output_memory_envelope_with_action_puts_action_before_memory)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_memory_data(f, "created", &e, NULL, 0, NOW), 0);
    assert_written(
        f, "{\"action\":\"created\",\"memory\":{\"id\":\"s-7\",\"local_id\":7,\"key\":\"k:7\",\"b"
           "ody\":\"body\",\"tags\":[],\"created_by\":{\"kind\":\"agent\",\"id\":null},\"updated_"
           "by\":{\"kind\":\"agent\",\"id\":null},\"created_at\":\"c\",\"updated_at\":"
           "\"u\",\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\",\"version_vector\":{}"
           ",\"links\":[]}}");
}

TEST(output_action_envelope_writes_the_memory_without_links)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_action_data(f, "updated", &e, NOW), 0);
    assert_written(
        f, "{\"action\":\"updated\",\"memory\":{\"id\":\"s-7\",\"local_id\":7,\"key\":\"k:7\",\"b"
           "ody\":\"body\",\"tags\":[],\"created_by\":{\"kind\":\"agent\",\"id\":null},\"updated_"
           "by\":{\"kind\":\"agent\",\"id\":null},\"created_at\":\"c\",\"updated_at\":"
           "\"u\",\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\",\"version_vector\":{}"
           "}}");
}

TEST(output_list_envelope_rejects_missing_arguments)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_list_data(NULL, 1, 1, &e, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_list_data(f, 1, 1, &e, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_list_data(f, 1, 1, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_list_data(f, 1, 1, &e, NULL, 1, NOW), -1);
    assert_written(f, "");
}

/* The search "boo" on the live bin with no filter. */
static SearchQuery given_search_query(void)
{
    SearchQuery q;

    memset(&q, 0, sizeof(q));
    q.query = "boo";
    q.filters.bin = STORE_BIN_LIVE;
    return q;
}

/* E2.1 (4): an empty search still carries both facets, empty. The query
   follows entries (decision:plan21-search-query-original-only). */
TEST(output_search_envelope_writes_empty_facets)
{
    SearchResult res = {.page = {.st = STORE_OK}};
    SearchQuery q = given_search_query();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), 0);
    assert_written(f, "{\"count\":0,\"total\":0,\"keys\":{\"total\":0,\"items\":["
                      "]},\"tags\":{\"total\":0,\"items\":[]},\"entries\":[]"
                      ",\"query\":{\"original\":{\"text\":\"boo\",\"tags\":[],\"bins\":[\"live\"],"
                      "\"time_range\":{\"field\":\"updated_at\",\"after\":null,\"before\":null}}},"
                      "\"ranked_by\":\"bm25\",\"filters\":[]}");
}

/* query.original carries the text, the tag filter, the one bin, and the
   updated_at bounds, each through the JSON escaper. */
TEST(output_search_envelope_echoes_the_query_filters)
{
    static const char *const tags[] = {"a\"b", "c"};
    SearchResult res = {.page = {.st = STORE_OK}};
    SearchQuery q = given_search_query();
    FILE *f = given_stream();

    q.query = "x\ny";
    q.filters.tags = tags;
    q.filters.ntags = 2U;
    q.filters.bin = STORE_BIN_DELETED;
    q.filters.since = "2026-01-01T00:00:00.000Z";
    q.filters.until = "2026-02-01T00:00:00.000Z";
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), 0);
    assert_written(
        f, "{\"count\":0,\"total\":0,\"keys\":{\"total\":0,\"items\":["
           "]},\"tags\":{\"total\":0,\"items\":[]},\"entries\":[],\"query\":{"
           "\"original\":{\"text\":\"x\\ny\",\"tags\":[\"a\\\"b\",\"c\"],"
           "\"bins\":[\"deleted\"],\"time_range\":{\"field\":\"updated_at\","
           "\"after\":\"2026-01-01T00:00:00.000Z\",\"before\":\"2026-02-01T00:00:00.000Z\"}}},"
           "\"ranked_by\":\"bm25\",\"filters\":[]}");
}

/* Two key items and two tag items, so the comma between items shows. */
static SearchResult given_search_facets(SearchKeyItem keys[2], SearchTagItem tags[2])
{
    SearchResult res = {.page = {.st = STORE_OK}};

    keys[0] = (SearchKeyItem){.key = (char *)"recs:books",
                              .sync_id = (char *)"s-1",
                              .label = (char *)"Book recommendations",
                              .updated_at = (char *)"u-1"};
    keys[1] = (SearchKeyItem){.key = (char *)"recs:bonds",
                              .sync_id = (char *)"s-2",
                              .label = (char *)"recs:bonds",
                              .updated_at = (char *)"u-2"};
    tags[0] = (SearchTagItem){.tag = (char *)"type:books", .count = 1};
    tags[1] = (SearchTagItem){.tag = (char *)"recs", .count = TAG_COUNT};
    res.keys = (SearchKeyFacet){.items = keys, .count = 2, .total = KEY_MATCHES};
    res.tags = (SearchTagFacet){.items = tags, .count = 2, .total = 4};
    return res;
}

/* E2.6: each key item carries key, id, label, updated_at; each
   tag item tag and count; total counts every match, not only the items. */
TEST(output_search_envelope_writes_the_facet_items)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    SearchQuery q = given_search_query();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), 0);
    assert_written(
        f, "{\"count\":0,\"total\":0,\"keys\":{\"total\":5,\"items\":["
           "{\"key\":\"recs:books\",\"id\":\"s-1\",\"label\":\"Book recommendations\","
           "\"updated_at\":\"u-1\"},{\"key\":\"recs:bonds\",\"id\":\"s-2\","
           "\"label\":\"recs:bonds\",\"updated_at\":\"u-2\"}]},\"tags\":{\""
           "total\":4,\"items\":[{\"tag\":\"type:books\",\"count\":1},{\"tag\":\"recs\",\"count"
           "\":3}]},\"entries\":[]"
           ",\"query\":{\"original\":{\"text\":\"boo\",\"tags\":[],\"bins\":[\"live\"],"
           "\"time_range\":{\"field\":\"updated_at\",\"after\":null,\"before\":null}}},"
           "\"ranked_by\":\"bm25\",\"filters\":[]}");
}

/* Stage 4 handover: every facet field goes through the JSON escaper. The
   label comes from a body, so it can hold anything; the others are guarded
   on import but not trusted here. */
TEST(output_search_envelope_escapes_every_facet_field)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    SearchQuery q = given_search_query();
    FILE *f = given_stream();

    res.keys.items[0] = (SearchKeyItem){.key = (char *)"k\x1b",
                                        .sync_id = (char *)"s\n",
                                        .label = (char *)"l\"\x1b[31m",
                                        .updated_at = (char *)"u\t"};
    res.tags.items[0].tag = (char *)"t\x01";
    res.keys.count = 1;
    res.tags.count = 1;
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), 0);
    assert_written(f, "{\"count\":0,\"total\":0,\"keys\":{\"total\":5,\"items\":["
                      "{\"key\":\"k\\u001b\",\"id\":\"s\\n\",\"label\":\"l\\\"\\u001b[31m\","
                      "\"updated_at\":\"u\\t\"}]},\"tags\":{\"total\":4,\"items\":[{\"tag\":\"t"
                      "\\u0001\",\"count\":1}]},\"entries\":[]"
                      ",\"query\":{\"original\":{\"text\":\"boo\",\"tags\":[],\"bins\":[\"live\"],"
                      "\"time_range\":{\"field\":\"updated_at\",\"after\":null,\"before\":null}}},"
                      "\"ranked_by\":\"bm25\",\"filters\":[]}");
}

TEST(output_search_envelope_rejects_missing_arguments)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    StoreNeighbor link = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED);
    SearchQuery q = given_search_query();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_search_data(NULL, &q, &res, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_search_data(f, &q, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 1, NOW), -1);
    res.page.count = 1;
    ASSERT_EQ_INT(output_search_data(f, &q, &res, &link, 1, NOW), -1);
    res.page.count = 0;
    res.keys.items = NULL;
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), -1);
    res.keys.count = 0;
    res.tags.items = NULL;
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), -1);
    res.tags.count = 0;
    res.tags.items = tags;
    ASSERT_EQ_INT(output_search_data(f, NULL, &res, NULL, 0, NOW), -1);
    q.query = NULL;
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), -1);
    q = given_search_query();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the guard under test
    q.filters.bin = (StoreBin)(STORE_BIN_DELETED + 1);
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), -1);
    q = given_search_query();
    q.filters.ntags = 1U;
    ASSERT_EQ_INT(output_search_data(f, &q, &res, NULL, 0, NOW), -1);
    assert_written(f, "");
}

/* E2.7: one keys line and one tags line, in the facet order. */
TEST(output_search_facets_human_lists_keys_then_tags)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    FILE *f = given_stream();

    res.keys.total = 2;
    res.tags.total = 2;
    ASSERT_EQ_INT(output_search_facets_human(f, &res), 0);
    assert_written(f, "keys: recs:books, recs:bonds\ntags: type:books (1), recs (3)\n");
}

/* n key items k0, k1, ... (n <= MAX_KEY_ITEMS) with the given total. */
static SearchResult given_key_items(SearchKeyItem keys[MAX_KEY_ITEMS], size_t n, size_t total)
{
    // cppcheck-suppress constVariable ; SearchKeyItem.key is char *, so const would need a
    // cast-qual cast
    static char *const names[] = {(char *)"k0", (char *)"k1", (char *)"k2",  (char *)"k3",
                                  (char *)"k4", (char *)"k5", (char *)"k6",  (char *)"k7",
                                  (char *)"k8", (char *)"k9", (char *)"k10", (char *)"k11"};
    SearchResult res = {.page = {.st = STORE_OK}};
    size_t i = 0;

    for (i = 0; i < n; i++) {
        keys[i] = (SearchKeyItem){.key = names[i]};
    }
    res.keys = (SearchKeyFacet){.items = keys, .count = n, .total = total};
    return res;
}

/* E2.7 + decision:plan18-human-facet-line: 10 items, then the rest of the
   total, not the rest of the items. */
TEST(output_search_facets_human_caps_at_ten_and_counts_the_rest_of_the_total)
{
    SearchKeyItem keys[MAX_KEY_ITEMS];
    SearchResult res = given_key_items(keys, MAX_KEY_ITEMS, MANY_MATCHES);
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_search_facets_human(f, &res), 0);
    assert_written(f, "keys: k0, k1, k2, k3, k4, k5, k6, k7, k8, k9, \xe2\x80\xa6 and 50 more\n");
}

TEST(output_search_facets_human_shows_ten_without_more)
{
    SearchKeyItem keys[MAX_KEY_ITEMS];
    SearchResult res = given_key_items(keys, HUMAN_CAP, HUMAN_CAP);
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_search_facets_human(f, &res), 0);
    assert_written(f, "keys: k0, k1, k2, k3, k4, k5, k6, k7, k8, k9\n");
}

/* Fewer items than 10 (a small --facet-limit) still count the whole total. */
TEST(output_search_facets_human_says_more_when_the_total_passes_the_items)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_search_facets_human(f, &res), 0);
    assert_written(f, "keys: recs:books, recs:bonds, \xe2\x80\xa6 and 3 more\n"
                      "tags: type:books (1), recs (3), \xe2\x80\xa6 and 2 more\n");
}

TEST(output_search_facets_human_omits_an_empty_keys_line)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    FILE *f = given_stream();

    res.keys = (SearchKeyFacet){0};
    res.tags.total = 2;
    ASSERT_EQ_INT(output_search_facets_human(f, &res), 0);
    assert_written(f, "tags: type:books (1), recs (3)\n");
}

TEST(output_search_facets_human_omits_an_empty_tags_line)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    FILE *f = given_stream();

    res.keys.total = 2;
    res.tags = (SearchTagFacet){0};
    ASSERT_EQ_INT(output_search_facets_human(f, &res), 0);
    assert_written(f, "keys: recs:books, recs:bonds\n");
}

/* decision:plan18c-mask-c1: C0, DEL and C1 in a stored key or tag become '?'. */
TEST(output_search_facets_human_masks_keys_and_tags)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    FILE *f = given_stream();

    res.keys.items[0].key = (char *)"k\x1b[2J\n";
    res.tags.items[0].tag = (char *)"t\xc2\x9b\x7f";
    res.keys.count = 1;
    res.keys.total = 1;
    res.tags.count = 1;
    res.tags.total = 1;
    ASSERT_EQ_INT(output_search_facets_human(f, &res), 0);
    assert_written(f, "keys: k?[2J?\ntags: t?? (1)\n");
}

TEST(output_search_facets_human_rejects_missing_arguments)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_search_facets_human(NULL, &res), -1);
    ASSERT_EQ_INT(output_search_facets_human(f, NULL), -1);
    res.keys.items = NULL;
    ASSERT_EQ_INT(output_search_facets_human(f, &res), -1);
    res.keys.count = 0;
    res.tags.items = NULL;
    ASSERT_EQ_INT(output_search_facets_human(f, &res), -1);
    assert_written(f, "");
}

TEST(output_tags_envelope_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_tags_data(NULL, NULL, 0), -1);
    ASSERT_EQ_INT(output_tags_data(f, NULL, 1), -1);
    assert_written(f, "");
}

TEST(output_links_write_envelope_rejects_missing_arguments)
{
    StoreNeighbor link = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED);
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_links_write_data(NULL, "created", &link, 1, NOW), -1);
    ASSERT_EQ_INT(output_links_write_data(f, NULL, &link, 1, NOW), -1);
    ASSERT_EQ_INT(output_links_write_data(f, "created", &link, 1, NULL), -1);
    ASSERT_EQ_INT(output_links_write_data(f, "created", NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_links_write_envelope_rejects_a_link_without_a_sync_id)
{
    StoreNeighbor link = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED);
    FILE *f = given_stream();

    link.neighbor_sync_id = NULL;
    ASSERT_EQ_INT(output_links_write_data(f, "created", &link, 1, NOW), -1);
    (void)fclose(f);
}

TEST(output_links_write_envelope_names_both_supersedes_directions)
{
    StoreNeighbor links[] = {given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_SUPERSEDES),
                             given_link(SUBJECT_ID, NEIGHBOR_ID, STORE_EDGE_SUPERSEDES)};
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_links_write_data(f, "created", links, 2, NOW), 0);
    assert_written(
        f, "{\"action\":\"created\",\"count\":2,\"links\":[{\"id\":\"n-3\",\"local_id\":3,\"key\""
           ":null,\"type\":\"supersedes\",\"bin\":\"live\",\"preview\":\"nb\"},{\"id\":\"n-3\","
           "\"local_"
           "id\":3,\"key\":null,\"type\":\"superseded_by\",\"bin\":\"live\",\"preview\":\""
           "nb\"}]}");
}

TEST(output_related_envelope_rejects_missing_arguments)
{
    Entry subject = given_entry();
    Entry no_id = given_entry();
    FILE *f = given_stream();

    no_id.sync_id = NULL;
    ASSERT_EQ_INT(output_related_data(NULL, &subject, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_related_data(f, &subject, NULL, 0, NULL), -1);
    ASSERT_EQ_INT(output_related_data(f, NULL, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_related_data(f, &no_id, NULL, 0, NOW), -1);
    ASSERT_EQ_INT(output_related_data(f, &subject, NULL, 1, NOW), -1);
    assert_written(f, "");
}

TEST(output_import_envelope_rejects_missing_arguments)
{
    StoreImportResult counts = {0};
    FILE *f = given_stream();

    StoreImportResult no_conflicts = {.nconflicts = 1};
    StoreImportResult no_rejects = {.nrejected = 1};

    ASSERT_EQ_INT(output_import_data(NULL, &counts), -1);
    ASSERT_EQ_INT(output_import_data(f, NULL), -1);
    ASSERT_EQ_INT(output_import_data(f, &no_conflicts), -1);
    ASSERT_EQ_INT(output_import_data(f, &no_rejects), -1);
    assert_written(f, "");
}

/* A reject with no field name and a broken conflict row both stop the reply. */
TEST(output_import_envelope_rejects_an_incomplete_item)
{
    StoreImportReject no_field = {.source_local_id = 1, .field = NULL};
    StoreConflict broken = given_conflict();
    StoreImportResult bad_reject = {.rejected = &no_field, .nrejected = 1};
    StoreImportResult bad_conflict = {.conflicts = &broken, .nconflicts = 1};
    FILE *f = given_stream();

    broken.sync_id = NULL;
    ASSERT_EQ_INT(output_import_data(f, &bad_reject), -1);
    ASSERT_EQ_INT(output_import_data(f, &bad_conflict), -1);
    (void)fclose(f);
}

TEST(output_device_envelopes_reject_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_device_data(NULL, "d"), -1);
    ASSERT_EQ_INT(output_device_data(f, NULL), -1);
    ASSERT_EQ_INT(output_device_new_data(NULL, "d", NULL), -1);
    ASSERT_EQ_INT(output_device_new_data(f, NULL, NULL), -1);
    assert_written(f, "");
}

static StoreBrainId given_brain_ids(void)
{
    StoreBrainId ids = {0};

    (void)snprintf(ids.id, sizeof(ids.id), "11111111-1111-7111-8111-111111111111");
    (void)snprintf(ids.previous, sizeof(ids.previous), "22222222-2222-7222-8222-222222222222");
    return ids;
}

TEST(output_brain_new_id_envelope_rejects_missing_arguments)
{
    StoreBrainId ids = given_brain_ids();
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_brain_new_id_data(NULL, &ids), -1);
    ASSERT_EQ_INT(output_brain_new_id_data(f, NULL), -1);
    assert_written(f, "");
}

TEST(output_conflicts_envelope_rejects_missing_arguments)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_conflicts_data(NULL, NULL, 0), -1);
    ASSERT_EQ_INT(output_conflicts_data(f, NULL, 1), -1);
    assert_written(f, "");
}

TEST(output_conflicts_envelope_lists_no_conflicts)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_conflicts_data(f, NULL, 0), 0);
    assert_written(f, "{\"count\":0,\"conflicts\":[]}");
}

TEST(output_conflicts_envelope_separates_conflicts_with_a_comma)
{
    StoreConflict rows[] = {given_conflict(), given_conflict()};
    FILE *f = given_stream();

    rows[1].id = 2;
    ASSERT_EQ_INT(output_conflicts_data(f, rows, 2), 0);
    assert_written(
        f, "{\"count\":2,\"conflicts\":[{\"id\":1,\"memory_id\":\"s-1\",\"reason\":\"key_clash\","
           "\"local\":{},\"incoming\":{},\"created_at\":\"c\"},{\"id\":2,\"memory_id\":\"s-1\",\"r"
           "eason\":\"key_clash\",\"local\":{},\"incoming\":{},\"created_at\":\"c\"}]}");
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
    ASSERT_EQ_INT(output_conflicts_data(f, &rows[0], 1), -1);
    ASSERT_EQ_INT(output_conflicts_data(f, &rows[1], 1), -1);
    ASSERT_EQ_INT(output_conflicts_data(f, &rows[2], 1), -1);
    ASSERT_EQ_INT(output_conflicts_data(f, &rows[3], 1), -1);
    ASSERT_EQ_INT(output_conflicts_data(f, &rows[4], 1), -1);
    (void)fclose(f);
}

TEST(output_entries_envelope_lists_no_entries)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_entries_data(f, "accepted", NULL, 0, NOW), 0);
    assert_written(f, "{\"action\":\"accepted\",\"count\":0,\"entries\":[]}");
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

/* decision:plan18c-mask-c1: U+0080..U+009F (C2 80..C2 9F) print as one '?'. */
TEST(output_preview_masks_a_c1_control)
{
    Entry e = given_entry();
    FILE *f = given_stream();

    e.body = (char *)"a\xc2\x9b"
                     "b";
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, "7 | k:7 |  | a?b | u | \n");
}

TEST(output_text_human_masks_c1_controls)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_text_human(f,
                                    "a\xc2\x80"
                                    "b\xc2\x9b"
                                    "c\xc2\x9f"
                                    "d",
                                    false),
                  0);
    assert_written(f, "a?b?c?d");
}

/* Only C2 80..C2 9F: the next code point, another lead byte, or a stray C2 stay. */
TEST(output_text_human_keeps_other_two_byte_text)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_text_human(f,
                                    "\xc2\xa0"
                                    "\xc3\x80"
                                    "\xc2"
                                    "A",
                                    false),
                  0);
    assert_written(f, "\xc2\xa0"
                      "\xc3\x80"
                      "\xc2"
                      "A");
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

/* decision:import-control-bytes-followup: a peer's stored text is not trusted. */
TEST(output_entry_human_line_masks_control_bytes_in_key_tags_and_updated_at)
{
    char *tags[] = {(char *)"t\x1b", (char *)"\x7f\n"};
    Entry e = given_entry();
    FILE *f = given_stream();

    e.key = (char *)"k\x1b[2J\n";
    e.tags = tags;
    e.ntags = 2;
    e.updated_at = (char *)"u\x1b[31m\n";
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, NOW), 0);
    assert_written(f, "7 | k?[2J? | t?,?? | body | u?[31m? | \n");
}

TEST(output_tags_human_masks_control_bytes)
{
    TagCount tag = {.name = (char *)"t\x1b[1m\n", .count = TAG_COUNT};
    FILE *f = given_stream();

    ASSERT_EQ_INT(output_tags_human(f, &tag, 1), 0);
    assert_written(f, "t?[1m?\t3\n");
}

TEST(output_related_human_masks_a_neighbor_key)
{
    StoreNeighbor link = given_link(SUBJECT_ID, SUBJECT_ID, STORE_EDGE_RELATED);
    FILE *f = given_stream();

    link.neighbor_key = (char *)"n\x1b]0;x\x07\n";
    ASSERT_EQ_INT(output_related_human(f, &link, 1, NOW), 0);
    assert_written(f, "Related:\nrelated n?]0;x?? nb\n");
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

    return output_action_data(f, "updated", &e, NOW);
}

static int write_entries_envelope(FILE *f)
{
    Entry entries[] = {given_full_entry(), given_full_entry()};

    return output_entries_data(f, "deleted", entries, 2, NOW);
}

static int write_get_envelope(FILE *f)
{
    Entry e = given_full_entry();
    StoreNeighbor links[2];

    given_two_links(links);
    return output_memory_data(f, NULL, &e, links, 2, NOW);
}

static int write_add_envelope(FILE *f)
{
    Entry e = given_full_entry();

    return output_memory_data(f, "created", &e, NULL, 0, NOW);
}

static int write_list_envelope(FILE *f)
{
    Entry entries[] = {given_full_entry(), given_full_entry()};
    StoreNeighbor links[2];

    given_two_links(links);
    return output_list_data(f, 2, 2, entries, links, 2, NOW);
}

static int write_search_envelope(FILE *f)
{
    Entry entries[] = {given_full_entry(), given_full_entry()};
    StoreNeighbor links[2];
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);

    static const char *const filter_tags[] = {"a", "b"};
    SearchQuery q = given_search_query();

    q.filters.tags = filter_tags;
    q.filters.ntags = 2U;
    q.filters.since = "2026-01-01T00:00:00.000Z";
    q.filters.until = "2026-02-01T00:00:00.000Z";
    given_two_links(links);
    res.page = (PageResult){.st = STORE_OK, .entries = entries, .count = 2, .total = 2};
    return output_search_data(f, &q, &res, links, 2, NOW);
}

static int write_search_facets_human(FILE *f)
{
    SearchKeyItem keys[2];
    SearchTagItem tags[2];
    SearchResult res = given_search_facets(keys, tags);

    return output_search_facets_human(f, &res);
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

    return output_tags_data(f, tags, 2);
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
    return output_links_write_data(f, "created", links, 2, NOW);
}

static int write_related_envelope_with_key(FILE *f)
{
    StoreNeighbor links[2];
    Entry subject = given_entry();

    given_two_links(links);
    return output_related_data(f, &subject, links, 2, NOW);
}

static int write_related_envelope_without_key(FILE *f)
{
    Entry subject = given_entry();

    subject.key = NULL;
    return output_related_data(f, &subject, NULL, 0, NOW);
}

static int write_related_human(FILE *f)
{
    StoreNeighbor links[2];

    given_two_links(links);
    return output_related_human(f, links, 2, NOW);
}

enum { REJECT_ROW_A = 7, REJECT_ROW_B = 9 };

static int write_import_envelope(FILE *f)
{
    StoreConflict conflicts[] = {given_conflict(), given_conflict()};
    StoreImportReject rejected[] = {{.source_local_id = REJECT_ROW_A, .field = "key"},
                                    {.source_local_id = REJECT_ROW_B, .field = "body_hash"}};
    StoreImportResult counts = {.inserted = 1, .updated = 2, .unchanged = 3};

    conflicts[1].id = 2;
    counts.conflicts = conflicts;
    counts.nconflicts = 2;
    counts.rejected = rejected;
    counts.nrejected = 2;
    return output_import_data(f, &counts);
}

static int write_device_human(FILE *f)
{
    return output_device_human(f, "dev");
}

static int write_device_envelope(FILE *f)
{
    return output_device_data(f, "dev");
}

static int write_device_new_envelope_with_previous(FILE *f)
{
    return output_device_new_data(f, "dev", "old");
}

static int write_device_new_envelope_without_previous(FILE *f)
{
    return output_device_new_data(f, "dev", NULL);
}

static int write_brain_new_id_envelope(FILE *f)
{
    StoreBrainId ids = given_brain_ids();

    return output_brain_new_id_data(f, &ids);
}

static int write_conflicts_envelope(FILE *f)
{
    StoreConflict rows[] = {given_conflict(), given_conflict()};

    return output_conflicts_data(f, rows, 2);
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

TEST(output_entries_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_entries_envelope);
}

TEST(output_memory_envelope_get_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_get_envelope);
}

TEST(output_list_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_list_envelope);
}

TEST(output_search_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_search_envelope);
}

TEST(output_search_facets_human_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_search_facets_human);
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

/* 21 stage 1 grill: the import reply names each row it opened a conflict for or
   refused, so an agent need not run conflicts after every import. */
TEST(output_import_envelope_lists_conflicts_and_rejected_rows)
{
    FILE *f = given_stream();

    ASSERT_EQ_INT(write_import_envelope(f), 0);
    assert_written(f,
                   "{\"action\":\"imported\",\"inserted\":1,\"updated\":2,\"unchanged\":3,"
                   "\"conflicts\":{\"count\":2,\"items\":[{\"id\":1,\"memory_id\":\"s-1\","
                   "\"reason\":\"key_clash\",\"local\":{},\"incoming\":{},\"created_at\":\"c\"},"
                   "{\"id\":2,\"memory_id\":\"s-1\",\"reason\":\"key_clash\",\"local\":{},"
                   "\"incoming\":{},\"created_at\":\"c\"}]},\"rejected\":{\"count\":2,\"items\":["
                   "{\"source_local_id\":7,\"field\":\"key\"},{\"source_local_id\":9,"
                   "\"field\":\"body_hash\"}]}}");
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

TEST(output_brain_new_id_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_brain_new_id_envelope);
}

TEST(output_conflicts_envelope_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_conflicts_envelope);
}

TEST(output_memory_envelope_add_reports_every_failed_write)
{
    assert_reports_every_failed_write(write_add_envelope);
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
    RUN_TEST(output_entry_json_rejects_a_non_canonical_version_vector);
    RUN_TEST(output_entry_json_rejects_a_bad_actor);
    RUN_TEST(output_entry_json_renders_null_text_fields_as_empty_strings);
    RUN_TEST(output_action_envelope_rejects_missing_arguments);
    RUN_TEST(output_entries_envelope_rejects_missing_arguments);
    RUN_TEST(output_entries_envelope_separates_entries_with_a_comma);
    RUN_TEST(output_memory_envelope_rejects_missing_arguments);
    RUN_TEST(output_memory_envelope_without_action_lists_only_the_subjects_links);
    RUN_TEST(output_memory_envelope_with_action_puts_action_before_memory);
    RUN_TEST(output_action_envelope_writes_the_memory_without_links);
    RUN_TEST(output_list_envelope_rejects_missing_arguments);
    RUN_TEST(output_search_envelope_writes_empty_facets);
    RUN_TEST(output_search_envelope_echoes_the_query_filters);
    RUN_TEST(output_search_envelope_writes_the_facet_items);
    RUN_TEST(output_search_envelope_escapes_every_facet_field);
    RUN_TEST(output_search_envelope_rejects_missing_arguments);
    RUN_TEST(output_search_facets_human_lists_keys_then_tags);
    RUN_TEST(output_search_facets_human_caps_at_ten_and_counts_the_rest_of_the_total);
    RUN_TEST(output_search_facets_human_shows_ten_without_more);
    RUN_TEST(output_search_facets_human_says_more_when_the_total_passes_the_items);
    RUN_TEST(output_search_facets_human_omits_an_empty_keys_line);
    RUN_TEST(output_search_facets_human_omits_an_empty_tags_line);
    RUN_TEST(output_search_facets_human_masks_keys_and_tags);
    RUN_TEST(output_search_facets_human_rejects_missing_arguments);
    RUN_TEST(output_tags_envelope_rejects_missing_arguments);
    RUN_TEST(output_links_write_envelope_rejects_missing_arguments);
    RUN_TEST(output_links_write_envelope_rejects_a_link_without_a_sync_id);
    RUN_TEST(output_links_write_envelope_names_both_supersedes_directions);
    RUN_TEST(output_related_envelope_rejects_missing_arguments);
    RUN_TEST(output_import_envelope_rejects_missing_arguments);
    RUN_TEST(output_import_envelope_lists_conflicts_and_rejected_rows);
    RUN_TEST(output_import_envelope_rejects_an_incomplete_item);
    RUN_TEST(output_device_envelopes_reject_missing_arguments);
    RUN_TEST(output_brain_new_id_envelope_rejects_missing_arguments);
    RUN_TEST(output_conflicts_envelope_rejects_missing_arguments);
    RUN_TEST(output_conflicts_envelope_lists_no_conflicts);
    RUN_TEST(output_conflicts_envelope_separates_conflicts_with_a_comma);
    RUN_TEST(output_conflicts_envelope_rejects_an_incomplete_conflict);
    RUN_TEST(output_entries_envelope_lists_no_entries);
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
    RUN_TEST(output_preview_masks_a_c1_control);
    RUN_TEST(output_text_human_masks_c1_controls);
    RUN_TEST(output_text_human_keeps_other_two_byte_text);
    RUN_TEST(output_preview_cuts_by_bytes_before_the_scalar_cap);
    RUN_TEST(output_entry_human_line_masks_control_bytes_in_key_tags_and_updated_at);
    RUN_TEST(output_tags_human_masks_control_bytes);
    RUN_TEST(output_related_human_masks_a_neighbor_key);
    RUN_TEST(output_related_human_rejects_missing_arguments);
    RUN_TEST(output_related_human_omits_an_empty_preview);
    RUN_TEST(failing_stream_fails_past_the_limit_and_stays_failed);
    RUN_TEST(output_json_string_reports_every_failed_write);
    RUN_TEST(output_entry_json_reports_every_failed_write);
    RUN_TEST(output_action_envelope_reports_every_failed_write);
    RUN_TEST(output_entries_envelope_reports_every_failed_write);
    RUN_TEST(output_memory_envelope_get_reports_every_failed_write);
    RUN_TEST(output_list_envelope_reports_every_failed_write);
    RUN_TEST(output_search_envelope_reports_every_failed_write);
    RUN_TEST(output_search_facets_human_reports_every_failed_write);
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
    RUN_TEST(output_brain_new_id_envelope_reports_every_failed_write);
    RUN_TEST(output_conflicts_envelope_reports_every_failed_write);
    RUN_TEST(output_memory_envelope_add_reports_every_failed_write);
    RUN_TEST(output_rebuilt_json_reports_every_failed_write);
    RUN_TEST(output_rebuilt_human_reports_every_failed_write);
    RUN_TEST(output_entry_human_line_reports_every_failed_write);
}
