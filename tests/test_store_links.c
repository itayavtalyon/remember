#include "harness.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    NONZERO_SENTINEL = 7, /* pre-set out-count; store must reset it to 0 */
    ALLOC_SWEEP_MAX = 24  /* alloc-failure injection points to sweep */
};

/*
 * Store unit tests for P7 entry_links (schema v3 + graph/rekey ports).
 * Black-box against store.h; schema via sqlite3 CLI inspect.
 */

static void assert_query_is(const char *db, QueryExpect check)
{
    char *row = harness_sqlite_query_line(db, check.sql);

    ASSERT_TRUE(row != NULL);
    ASSERT_STREQ(row != NULL ? row : "", check.want);
    free(row);
}

TEST(store_open_creates_entry_links)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='entry_links';",
                .want = "entry_links"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT name FROM sqlite_master WHERE type='index' AND name='entry_links_edge';",
            .want = "entry_links_edge"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT name FROM sqlite_master WHERE type='index' AND name='entry_links_to';",
            .want = "entry_links_to"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT COUNT(*) FROM pragma_table_info('entry_links') WHERE name='dir';",
                .want = "0"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT CASE WHEN sql LIKE '%from_id != to_id%' OR sql LIKE '%from_id<>to_id%' "
                   "THEN 'ok' ELSE 'bad' END FROM sqlite_master WHERE name='entry_links';",
            .want = "ok"});
    free(db);
}

TEST(store_open_migrates_v2_to_v3)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    free(harness_sqlite_query_line(
        db, "CREATE TABLE entries(id INTEGER PRIMARY KEY, key TEXT, body TEXT NOT NULL, "
            "body_hash TEXT NOT NULL, source TEXT NOT NULL, created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL, expires_at TEXT);"
            "INSERT INTO entries(body, body_hash, source, created_at, updated_at) "
            "VALUES('v2 row','h','human','2026-01-01T00:00:00.000Z','2026-01-01T00:00:00.000Z');"
            "PRAGMA user_version=2;"));
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "2"});

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT body FROM entries WHERE id=1;", .want = "v2 row"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_links;", .want = "0"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT name FROM sqlite_master WHERE type='index' AND name='entry_links_edge';",
            .want = "entry_links_edge"});
    free(db);
}

static const char k_hash_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char k_hash_b[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char k_hash_c[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
static const char k_now[] = "2026-06-15T12:00:00.000Z";
static const char k_later[] = "2026-06-15T13:00:00.000Z";
static const char k_past[] = "2020-01-01T00:00:00.000Z";

static long long add_row(Store *s, const char *body, const char *hash, const char *key,
                         const char *expires)
{
    StoreAddAction act = STORE_ADD_CREATED;
    Entry e;
    long long id = 0;

    memset(&e, 0, sizeof(e));
    if (store_add(s, body, hash, key, NULL, 0U, "human", expires, k_now, &act, &e) != STORE_OK) {
        return 0;
    }
    id = e.id;
    store_entry_free(&e);
    return id;
}

static Store *open_temp(char **out_db)
{
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    *out_db = make_temp_db_path();
    if (*out_db == NULL) {
        return NULL;
    }
    err[0] = '\0';
    s = store_open(*out_db, err, sizeof(err));
    if (s == NULL) {
        free(*out_db);
        *out_db = NULL;
    }
    return s;
}

TEST(store_link_related_is_one_canonical_row)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreEdgeKind related = STORE_EDGE_RELATED;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, NULL, NULL);
    ASSERT_TRUE(a > 0 && b > 0 && a != b);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = a, .to_id = b}, related, k_now, &act, &stub),
        STORE_OK);
    ASSERT_EQ_INT((int)act, (int)STORE_LINK_CREATED);
    ASSERT_EQ_INT(stub.neighbor_id, b);
    store_neighbor_free(&stub);

    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_links;", .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT kind FROM entry_links;", .want = "related"});
    /* Canonical (min, max) regardless of call order. */
    if (a < b) {
        assert_query_is(db, (QueryExpect){.sql = "SELECT from_id FROM entry_links;", .want = "1"});
        assert_query_is(db, (QueryExpect){.sql = "SELECT to_id FROM entry_links;", .want = "2"});
    } else {
        assert_query_is(db, (QueryExpect){.sql = "SELECT from_id FROM entry_links;", .want = "2"});
        assert_query_is(db, (QueryExpect){.sql = "SELECT to_id FROM entry_links;", .want = "1"});
    }

    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = b, .to_id = a}, related, k_later, &act, &stub),
        STORE_OK);
    ASSERT_EQ_INT((int)act, (int)STORE_LINK_MERGED);
    store_neighbor_free(&stub);
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_links;", .want = "1"});
    store_close(s);
    free(db);
}

TEST(store_link_directed_and_kinds_independent)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, NULL, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_CITES, k_now, &act, &stub),
        STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_links;", .want = "2"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT COUNT(*) FROM entry_links WHERE kind='cites' AND from_id=1 AND to_id=2;",
            .want = "1"});
    store_close(s);
    free(db);
}

TEST(store_link_self_and_missing)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = a}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_ERR_SELF_LINK);
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = 99}, STORE_EDGE_RELATED,
                                k_now, &act, &stub),
                     STORE_ERR_NOT_FOUND);
    store_close(s);
    free(db);
}

TEST(store_link_supersedes_cycle)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    long long c = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "a", k_hash_a, NULL, NULL);
    b = add_row(s, "b", k_hash_b, NULL, NULL);
    c = add_row(s, "c", k_hash_c, NULL, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_SUPERSEDES,
                                k_now, &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = b, .to_id = c}, STORE_EDGE_SUPERSEDES,
                                k_now, &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = c, .to_id = a}, STORE_EDGE_SUPERSEDES,
                                k_now, &act, &stub),
                     STORE_ERR_CYCLE);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = b, .to_id = a}, STORE_EDGE_SUPERSEDES,
                                k_now, &act, &stub),
                     STORE_ERR_CYCLE);
    /* cites both ways is allowed */
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_CITES, k_now, &act, &stub),
        STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = b, .to_id = a}, STORE_EDGE_CITES, k_now, &act, &stub),
        STORE_OK);
    store_neighbor_free(&stub);
    store_close(s);
    free(db);
}

TEST(store_unlink_idempotent_and_pair)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *gone = NULL;
    size_t n = 0U;
    StoreEdgeKind related = STORE_EDGE_RELATED;
    StoreEdgeKind cites = STORE_EDGE_CITES;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, NULL, NULL);
    ASSERT_EQ_STATUS(store_unlink(s, a, b, &related, k_now, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 0);
    ASSERT_TRUE(gone == NULL);

    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = a, .to_id = b}, related, k_now, &act, &stub),
        STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = a, .to_id = b}, cites, k_now, &act, &stub), STORE_OK);
    store_neighbor_free(&stub);

    ASSERT_EQ_STATUS(store_unlink(s, b, a, &related, k_later, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_neighbors_free(gone, n);
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_links;", .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT kind FROM entry_links;", .want = "cites"});

    ASSERT_EQ_STATUS(store_unlink(s, a, b, NULL, k_later, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_neighbors_free(gone, n);
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_links;", .want = "0"});

    /* no-op unlink does not bump */
    {
        Entry ea;
        memset(&ea, 0, sizeof(ea));
        ASSERT_EQ_STATUS(store_get_any(s, a, &ea), STORE_OK);
        ASSERT_STREQ(ea.updated_at, k_later);
        store_entry_free(&ea);
        ASSERT_EQ_STATUS(store_unlink(s, a, b, &related, k_now, &gone, &n), STORE_OK);
        ASSERT_EQ_INT((int)n, 0);
        memset(&ea, 0, sizeof(ea));
        ASSERT_EQ_STATUS(store_get_any(s, a, &ea), STORE_OK);
        ASSERT_STREQ(ea.updated_at, k_later);
        store_entry_free(&ea);
    }
    store_close(s);
    free(db);
}

TEST(store_link_bumps_both_endpoints)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    Entry ea;
    Entry eb;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, NULL, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED,
                                k_later, &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    memset(&ea, 0, sizeof(ea));
    memset(&eb, 0, sizeof(eb));
    ASSERT_EQ_STATUS(store_get_any(s, a, &ea), STORE_OK);
    ASSERT_EQ_STATUS(store_get_any(s, b, &eb), STORE_OK);
    ASSERT_STREQ(ea.updated_at, k_later);
    ASSERT_STREQ(eb.updated_at, k_later);
    store_entry_free(&ea);
    store_entry_free(&eb);
    store_close(s);
    free(db);
}

TEST(store_neighbors_dir_and_trash)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    long long c = 0;
    long long d = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *rows = NULL;
    size_t n = 0U;
    StoreEdgeKind cites = STORE_EDGE_CITES;
    size_t i = 0;
    int saw_related = 0;
    int saw_c = 0;
    int saw_d = 0;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, NULL, k_past);
    c = add_row(s, "gamma", k_hash_c, NULL, NULL);
    d = add_row(s, "delta", k_hash_a, "k:d", NULL);
    /* hashes: alpha unique, beta unique, gamma unique; delta is keyed so hash may match alpha */

    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = a, .to_id = c}, STORE_EDGE_CITES, k_now, &act, &stub),
        STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = d, .to_id = a}, STORE_EDGE_CITES, k_now, &act, &stub),
        STORE_OK);
    store_neighbor_free(&stub);

    /* either-bin: expired b is visible */
    {
        Entry e;
        memset(&e, 0, sizeof(e));
        ASSERT_EQ_STATUS(store_get_any(s, b, &e), STORE_OK);
        ASSERT_TRUE(e.expires_at != NULL);
        store_entry_free(&e);
        memset(&e, 0, sizeof(e));
        ASSERT_EQ_STATUS(store_get(s, b, STORE_BIN_LIVE, k_now, &e), STORE_ERR_EXPIRED);
    }

    ASSERT_EQ_STATUS(store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 3);
    for (i = 0; i < n; i++) {
        if (rows[i].kind == STORE_EDGE_RELATED) {
            saw_related++;
            ASSERT_EQ_INT(rows[i].neighbor_id, b);
            ASSERT_TRUE(rows[i].neighbor_expires_at != NULL);
        } else if (rows[i].neighbor_id == c) {
            saw_c++;
            ASSERT_EQ_INT((int)rows[i].kind, (int)STORE_EDGE_CITES);
            ASSERT_EQ_INT(rows[i].from_id, a);
        } else if (rows[i].neighbor_id == d) {
            saw_d++;
            ASSERT_EQ_INT((int)rows[i].kind, (int)STORE_EDGE_CITES);
            ASSERT_EQ_INT(rows[i].to_id, a);
        }
    }
    ASSERT_EQ_INT(saw_related, 1);
    ASSERT_EQ_INT(saw_c, 1);
    ASSERT_EQ_INT(saw_d, 1);
    store_neighbors_free(rows, n);

    ASSERT_EQ_STATUS(store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_OUTGOING, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 2); /* related + outgoing cites */
    store_neighbors_free(rows, n);

    ASSERT_EQ_STATUS(store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_INCOMING, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 2); /* related + incoming cites */
    store_neighbors_free(rows, n);

    ASSERT_EQ_STATUS(store_list_neighbors(s, a, &cites, STORE_NEIGHBOR_ALL, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 2);
    store_neighbors_free(rows, n);

    store_close(s);
    free(db);
}

TEST(store_neighbors_cascade_and_survive_trash)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *rows = NULL;
    size_t n = 0U;
    Entry deleted;
    Entry *purged = NULL;
    size_t pn = 0U;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, NULL, k_past);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    ASSERT_EQ_STATUS(store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_neighbors_free(rows, n);

    /* restore keeps the edge */
    memset(&deleted, 0, sizeof(deleted));
    ASSERT_EQ_STATUS(store_update(s, b, NULL, false, NULL, NULL, false, NULL, 0U, true, NULL, false,
                                  STORE_BIN_EXPIRED, k_now, &deleted, NULL),
                     STORE_OK);
    store_entry_free(&deleted);
    ASSERT_EQ_STATUS(store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_neighbors_free(rows, n);

    /* re-expire and purge drops the edge */
    memset(&deleted, 0, sizeof(deleted));
    ASSERT_EQ_STATUS(store_update(s, b, NULL, false, NULL, NULL, false, NULL, 0U, true, k_past,
                                  false, STORE_BIN_LIVE, k_now, &deleted, NULL),
                     STORE_OK);
    store_entry_free(&deleted);
    ASSERT_EQ_STATUS(store_purge_trash(s, k_now, &purged, &pn), STORE_OK);
    ASSERT_EQ_INT((int)pn, 1);
    store_entry_free(&purged[0]);
    free(purged);
    ASSERT_EQ_STATUS(store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 0);
    store_neighbors_free(rows, n);
    store_close(s);
    free(db);
}

TEST(store_rekey_rename_promote_demote)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    Entry e;
    long long conflict = 0;
    StoreNeighbor *rows = NULL;
    size_t n = 0U;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "slot", k_hash_a, "old:key", NULL);
    b = add_row(s, "other", k_hash_b, NULL, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_rekey(s, 0, (RekeyKeys){.key_or_null = "old:key", .new_key_or_null = "new:key"},
                    STORE_BIN_LIVE, k_later, &e, &conflict),
        STORE_OK);
    ASSERT_EQ_INT(e.id, a);
    ASSERT_STREQ(e.key, "new:key");
    ASSERT_STREQ(e.created_at, k_now);
    ASSERT_STREQ(e.updated_at, k_later);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_ALL, k_later, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_neighbors_free(rows, n);

    /* promote keyless b */
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_rekey(s, b, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = "b:key"},
                                 STORE_BIN_LIVE, k_later, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.key, "b:key");
    store_entry_free(&e);

    /* demote a */
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_rekey(s, a, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = NULL},
                                 STORE_BIN_LIVE, k_later, &e, &conflict),
                     STORE_OK);
    ASSERT_TRUE(e.key == NULL);
    store_entry_free(&e);

    /* same-value bump */
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_rekey(s, b, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = "b:key"},
                                 STORE_BIN_LIVE, k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.updated_at, k_now);
    store_entry_free(&e);

    /* NEWKEY taken */
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_rekey(s, a, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = "b:key"},
                                 STORE_BIN_LIVE, k_later, &e, &conflict),
                     STORE_ERR_CONFLICT);
    ASSERT_EQ_INT(conflict, b);

    /* demote body-hash collision: add another keyless with same hash as a after demote.
       a is now keyless with hash_a. Can't add another keyless hash_a. Promote a first,
       add keyless hash_a? a already has hash_a and is keyless — adding another keyless
       hash_a would merge. Use a keyed row with hash_a, then demote into the existing
       keyless... a is already keyless hash_a. Create keyed with hash_c, then try to
       demote a keyed hash_a into... simpler: keyed c with hash_a (allowed), demote c. */
    {
        long long c = add_row(s, "slot-copy", k_hash_a, "c:key", NULL);
        memset(&e, 0, sizeof(e));
        conflict = 0;
        ASSERT_EQ_STATUS(store_rekey(s, c,
                                     (RekeyKeys){.key_or_null = NULL, .new_key_or_null = NULL},
                                     STORE_BIN_LIVE, k_later, &e, &conflict),
                         STORE_ERR_CONFLICT);
        ASSERT_EQ_INT(conflict, a);
    }

    store_close(s);
    free(db);
}

TEST(store_list_neighbors_for_page)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    long long c = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *rows = NULL;
    size_t n = 0U;
    long long ids[2];

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "a", k_hash_a, NULL, NULL);
    b = add_row(s, "b", k_hash_b, NULL, NULL);
    c = add_row(s, "c", k_hash_c, NULL, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = b, .to_id = c}, STORE_EDGE_CITES, k_now, &act, &stub),
        STORE_OK);
    store_neighbor_free(&stub);
    ids[0] = a;
    ids[1] = b;
    ASSERT_EQ_STATUS(store_list_neighbors_for(s, ids, 2U, k_now, &rows, &n), STORE_OK);
    /* a sees b; b sees a and c */
    ASSERT_EQ_INT((int)n, 3);
    store_neighbors_free(rows, n);
    store_close(s);
    free(db);
}

TEST(store_list_neighbors_for_empty_page)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    StoreNeighbor *rows = NULL;
    size_t n = NONZERO_SENTINEL;

    ASSERT_TRUE(s != NULL);
    ASSERT_EQ_STATUS(store_list_neighbors_for(s, NULL, 0U, k_now, &rows, &n), STORE_OK);
    ASSERT_TRUE(rows == NULL);
    ASSERT_EQ_INT((int)n, 0);
    store_close(s);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(store_list_neighbors_oom_keeps_key_and_trash)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    int i = 0;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, "slot:k", k_past);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);

    for (i = 0; i < ALLOC_SWEEP_MAX; i++) {
        StoreNeighbor *rows = NULL;
        size_t n = 0U;
        StoreStatus st = STORE_OK;

        store_test_fail_alloc_after(i);
        st = store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &n);
        store_test_fail_alloc_after(-1);
        if (st != STORE_OK) {
            ASSERT_EQ_INT((int)st, (int)STORE_ERR_OOM);
            store_neighbors_free(rows, n);
            continue;
        }
        ASSERT_EQ_INT((int)n, 1);
        if (rows == NULL) {
            ASSERT_TRUE(0);
            break;
        }
        ASSERT_STREQ(rows[0].neighbor_key != NULL ? rows[0].neighbor_key : "", "slot:k");
        ASSERT_STREQ(rows[0].neighbor_expires_at != NULL ? rows[0].neighbor_expires_at : "",
                     k_past);
        store_neighbors_free(rows, n);
    }
    store_close(s);
    free(db);
}

TEST(store_unlink_stub_load_failure_is_sqlite_not_oom)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *gone = NULL;
    size_t n = 0U;
    StoreStatus st = STORE_OK;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "alpha", k_hash_a, NULL, NULL);
    b = add_row(s, "beta", k_hash_b, NULL, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_RELATED, k_now,
                                &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);

    /* Unlink: prepare SELECT edges (0), then fill_stub → load_entry prepare (1). */
    store_test_fail_prepare_after(1);
    st = store_unlink(s, a, b, NULL, k_now, &gone, &n);
    store_test_fail_prepare_after(-1);
    store_neighbors_free(gone, n);
    ASSERT_EQ_INT((int)st, (int)STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

/* Sweep prepare/step/alloc faults across the graph mutators/readers so their
   error and OOM cleanup paths execute (100% line coverage idiom). Asserts no
   crash/leak under ASan; specific results are not the point. */
/* Fault-injection fuzz sweep: store_test_fail_*(i % N) strides / small counts are
   intentionally arbitrary tuning to reach distinct failure points. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST(store_links_fault_injection_sweep)
{
    char *db = NULL;
    Store *s = open_temp(&db);
    long long a = 0;
    long long b = 0;
    long long c = 0;
    int i = 0;

    ASSERT_TRUE(s != NULL);
    a = add_row(s, "sa", "0000000000000000000000000000000000000000000000000000000000000001", "ka",
                NULL);
    b = add_row(s, "sb", "0000000000000000000000000000000000000000000000000000000000000002", NULL,
                NULL);
    c = add_row(s, "sc", "0000000000000000000000000000000000000000000000000000000000000003", NULL,
                NULL);
    ASSERT_TRUE(a > 0 && b > 0 && c > 0);

    for (i = 0; i < 20; i++) {
        StoreLinkAction act = STORE_LINK_CREATED;
        StoreNeighbor stub;
        StoreNeighbor *rows = NULL;
        StoreNeighbor *gone = NULL;
        size_t n = 0U;
        Entry e;
        long long conflict = 0;
        long long ids[2];

        store_test_fail_alloc_after(-1);
        store_test_fail_prepare_after(-1);
        store_test_fail_step_after(-1);

        memset(&stub, 0, sizeof(stub));
        store_test_fail_prepare_after(i % 8);
        if (store_link(s, (StoreEdge){.from_id = a, .to_id = b}, STORE_EDGE_CITES, k_now, &act,
                       &stub) == STORE_OK) {
            store_neighbor_free(&stub);
        }
        store_test_fail_prepare_after(-1);
        memset(&stub, 0, sizeof(stub));
        store_test_fail_step_after(i % 6);
        if (store_link(s, (StoreEdge){.from_id = a, .to_id = c}, STORE_EDGE_RELATED, k_now, &act,
                       &stub) == STORE_OK) {
            store_neighbor_free(&stub);
        }
        store_test_fail_step_after(-1);

        store_test_fail_prepare_after(i % 7);
        (void)store_unlink(s, a, b, NULL, k_now, &gone, &n);
        store_neighbors_free(gone, n);
        store_test_fail_prepare_after(-1);

        store_test_fail_step_after(i % 5);
        (void)store_list_neighbors(s, a, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &n);
        store_neighbors_free(rows, n);
        rows = NULL;
        n = 0U;
        store_test_fail_step_after(-1);
        store_test_fail_alloc_after(i % 6);
        ids[0] = a;
        ids[1] = c;
        (void)store_list_neighbors_for(s, ids, 2U, k_now, &rows, &n);
        store_neighbors_free(rows, n);
        store_test_fail_alloc_after(-1);

        memset(&e, 0, sizeof(e));
        store_test_fail_step_after(i % 6);
        (void)store_rekey(s, a, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = NULL},
                          STORE_BIN_LIVE, k_now, &e, &conflict); /* clear: load_body_hash */
        store_entry_free(&e);
        store_test_fail_step_after(-1);
        memset(&e, 0, sizeof(e));
        store_test_fail_prepare_after(i % 5);
        (void)store_rekey(s, 0, (RekeyKeys){.key_or_null = "ka", .new_key_or_null = "kz"},
                          STORE_BIN_LIVE, k_now, &e, &conflict); /* set/rename */
        store_entry_free(&e);
        store_test_fail_prepare_after(-1);
    }
    store_test_fail_alloc_after(-1);
    store_test_fail_prepare_after(-1);
    store_test_fail_step_after(-1);
    store_close(s);
    free(db);
    ASSERT_TRUE(1);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
#endif /* REMEMBER_TEST_HOOKS */

void register_store_link_tests(void)
{
    RUN_TEST(store_open_creates_entry_links);
    RUN_TEST(store_open_migrates_v2_to_v3);
    RUN_TEST(store_link_related_is_one_canonical_row);
    RUN_TEST(store_link_directed_and_kinds_independent);
    RUN_TEST(store_link_self_and_missing);
    RUN_TEST(store_link_supersedes_cycle);
    RUN_TEST(store_unlink_idempotent_and_pair);
    RUN_TEST(store_link_bumps_both_endpoints);
    RUN_TEST(store_neighbors_dir_and_trash);
    RUN_TEST(store_neighbors_cascade_and_survive_trash);
    RUN_TEST(store_rekey_rename_promote_demote);
    RUN_TEST(store_list_neighbors_for_page);
    RUN_TEST(store_list_neighbors_for_empty_page);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(store_list_neighbors_oom_keeps_key_and_trash);
    RUN_TEST(store_unlink_stub_load_failure_is_sqlite_not_oom);
    RUN_TEST(store_links_fault_injection_sweep);
#endif
}
