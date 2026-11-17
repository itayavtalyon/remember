#include "brain_list.h"
#include "brain_note.h"
#include "util.h"

#include "failing_stream.h"
#include "harness.h"
#include "register.h"
#include "remember_app.h"
#include "store.h"
#include "test.h"
#include "test_alloc.h"

#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    PATH_CAP = 512,
    ID_CAP = 64,
    CHILDREN = 8,
    SMALL_BUF = 8,
    TEXT_CAP = 2048,
    MANY_ROWS = 300,
    MANY_CAP = 64 * 1024,
    FAULTS_PER_KIND = 60,
    ENV_PAD = 7,
    LONG_NAME = 300,
    LOCK_HOLD_MS = 300,
    LIST_LOCK_HOLD_MS = 900,
    NS_PER_MS = 1000000,
    LIST_WAIT_MS = 5000,
    ODD_BUSY_MS = 1234,
    SHORT_BUSY_MS = 50,
    SMALL_ROW = 96
};

/* usleep is not in POSIX 2008, which the build asks for (_POSIX_C_SOURCE=200809L). ms < 1000. */
static void pause_ms(long ms)
{
    const struct timespec pause = {.tv_nsec = ms * NS_PER_MS};

    (void)nanosleep(&pause, NULL);
}

#define ID_A "01a0d90a-e71c-7000-8000-000000000001"
#define ID_B "01a0d90a-e71c-7000-8000-000000000002"
#define ID_MINTED "01a0d90a-e71c-7000-8000-0000000000ff"
#define NOW "2026-11-11T12:00:00.000Z"
#define LATER "2026-11-11T13:00:00.000Z"

/* What the probe answers, and how often it was asked. */
typedef struct {
    int stills;
    int mints;
    int mint_fails;
    int still; /* 1, 0 or -1, as BrainProbe.still answers */
} Probe;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the BrainProbe callback signature
static int probe_still(void *ctx, const char *path, const char *brain_id)
{
    Probe *p = (Probe *)ctx;

    (void)path;
    (void)brain_id;
    p->stills++;
    return p->still;
}

static int probe_mint(void *ctx, char *out, size_t cap)
{
    Probe *p = (Probe *)ctx;

    p->mints++;
    if (p->mint_fails > 0 || cap <= strlen(ID_MINTED)) {
        return -1;
    }
    memcpy(out, ID_MINTED, strlen(ID_MINTED) + 1U);
    return 0;
}

static BrainProbe given_probe(Probe *p)
{
    return (BrainProbe){.still = probe_still, .mint = probe_mint, .ctx = p};
}

/* Row i, or an empty row when the list is shorter: a wrong count then fails an assert and
   does not read past the rows. */
static const BrainRow *row_at(const BrainList *list, size_t index)
{
    static const BrainRow k_no_row = {
        .path = (char *)"", .brain_id = (char *)"", .added_at = (char *)""};

    return index < list->count ? &list->rows[index] : &k_no_row;
}

/* A fixture step that cannot be done ends the run, as harness_side_path does. */
static _Noreturn void fixture_failed(const char *what)
{
    (void)fprintf(stderr, "test_brain_list: fixture failed: %s\n", what);
    abort();
}

/* A fresh folder for one test; free the result. */
__attribute__((returns_nonnull)) static char *given_dir(void)
{
    char *db = make_temp_db_path();
    char *dir = db != NULL ? dir_of_path(db) : NULL;

    if (dir == NULL) {
        fixture_failed("temp folder");
    }
    free(db);
    return dir;
}

__attribute__((returns_nonnull)) static char *given_path(const char *dir, const char *name)
{
    char *out = (char *)malloc(PATH_CAP);

    if (out == NULL || snprintf(out, PATH_CAP, "%s/%s", dir, name) >= PATH_CAP) {
        fixture_failed("path");
    }
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) two names of one kind, read in order
static void given_bytes(const char *path, const char *bytes, size_t len)
{
    size_t wrote = 0;
    int closed = 0;
    /* macOS fopen has no 'e' (O_CLOEXEC) mode; the test process is short-lived. */
    // NOLINTNEXTLINE(android-cloexec-fopen)
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        fixture_failed("fopen");
    }
    wrote = fwrite(bytes, 1U, len, file);
    closed = fclose(file);
    ASSERT_EQ_INT((long long)wrote, (long long)len);
    ASSERT_EQ_INT(closed, 0);
}

static void given_text(const char *path, const char *text)
{
    given_bytes(path, text, strlen(text));
}

/* The file at json_path lists one row per (path, id) given, in order. */
static void given_rows(const char *json_path, const char *const *paths, const char *const *ids,
                       size_t n)
{
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    size_t i = 0;

    for (i = 0; i < n; i++) {
        BrainListCode code = brain_list_record(
            json_path, (BrainRecordIn){.real_path = paths[i], .brain_id = ids[i], .now = NOW},
            &probe, (BrainRecordOut){.minted = minted, .minted_cap = sizeof(minted)});

        ASSERT_EQ_STATUS(code, BRAIN_LIST_ADDED);
    }
}

static void given_one_row(const char *json_path, const char *path, const char *id)
{
    given_rows(json_path, &path, &id, 1U);
}

static BrainListCode record_at(const char *json_path, const char *path, const char *id,
                               const BrainProbe *probe, char *minted)
{
    return brain_list_record(
        json_path,
        (BrainRecordIn){
            .real_path = path, .brain_id = id, .now = LATER, .lock_timeout_ms = LIST_WAIT_MS},
        probe, (BrainRecordOut){.minted = minted, .minted_cap = ID_CAP});
}

__attribute__((returns_nonnull)) static char *file_text(const char *path)
{
    size_t len = 0;
    char *text = harness_read_file(path, &len);

    if (text == NULL) {
        fixture_failed("read file");
    }
    return text;
}

static const char k_doc_one[] =
    "{\"format\":\"remember-brains\",\"version\":1,\"brains\":[{\"path\":\"/tmp/a.db\","
    "\"brain_id\":\"" ID_A "\",\"added_at\":\"" NOW "\"}]}\n";

TEST(brain_list_first_record_adds_one_row_and_a_second_adds_none)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    BrainList list = {0};
    char *text = NULL;

    ASSERT_EQ_STATUS(
        brain_list_record(file,
                          (BrainRecordIn){.real_path = "/tmp/a.db", .brain_id = ID_A, .now = NOW},
                          &probe, (BrainRecordOut){.minted = minted, .minted_cap = sizeof(minted)}),
        BRAIN_LIST_ADDED);
    ASSERT_EQ_STATUS(
        brain_list_record(file,
                          (BrainRecordIn){.real_path = "/tmp/a.db", .brain_id = ID_A, .now = LATER},
                          &probe, (BrainRecordOut){.minted = minted, .minted_cap = sizeof(minted)}),
        BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, "/tmp/a.db");
    ASSERT_STREQ(row_at(&list, 0)->brain_id, ID_A);
    ASSERT_STREQ(row_at(&list, 0)->added_at, NOW);
    brain_list_free(&list);
    text = file_text(file);
    ASSERT_STREQ(text, k_doc_one);
    free(text);
    free(file);
    free(dir);
}

TEST(brain_list_file_mode_is_0600)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    char *lock = given_path(dir, "brains.json.lock");
    struct stat st;

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_INT(stat(file, &st), 0);
    ASSERT_EQ_INT((unsigned)st.st_mode & (unsigned)PERM_BITS_MASK, DB_FILE_PERMS);
    ASSERT_EQ_INT(stat(lock, &st), 0);
    ASSERT_EQ_INT((unsigned)st.st_mode & (unsigned)PERM_BITS_MASK, DB_FILE_PERMS);
    free(lock);
    free(file);
    free(dir);
}

TEST(brain_list_write_is_a_temp_file_renamed_over_brains_json)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    char *tmp = given_path(dir, "brains.json.tmp");
    struct stat before;
    struct stat after;
    char old[sizeof(k_doc_one)];
    int fd = -1;
    ssize_t got = 0;

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_INT(stat(file, &before), 0);
    fd = open(file, O_RDONLY | O_CLOEXEC);
    ASSERT_TRUE(fd >= 0);
    given_one_row(file, "/tmp/b.db", ID_B);
    ASSERT_EQ_INT(stat(file, &after), 0);
    ASSERT_TRUE(after.st_ino != before.st_ino);
    got = read(fd, old, sizeof(old) - 1U);
    ASSERT_EQ_INT(got, (long long)(sizeof(k_doc_one) - 1U));
    old[got] = '\0';
    ASSERT_STREQ(old, k_doc_one);
    ASSERT_EQ_INT(close(fd), 0);
    ASSERT_TRUE(access(tmp, F_OK) != 0);
    free(tmp);
    free(file);
    free(dir);
}

TEST(brain_list_stale_temp_symlink_is_replaced_not_followed)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    char *tmp = given_path(dir, "brains.json.tmp");
    char *victim = given_path(dir, "victim");
    char *text = NULL;

    given_text(victim, "keep\n");
    ASSERT_EQ_INT(symlink(victim, tmp), 0);
    given_one_row(file, "/tmp/a.db", ID_A);
    text = file_text(victim);
    ASSERT_STREQ(text, "keep\n");
    free(text);
    ASSERT_TRUE(access(tmp, F_OK) != 0);
    text = file_text(file);
    ASSERT_STREQ(text, k_doc_one);
    free(text);
    free(victim);
    free(tmp);
    free(file);
    free(dir);
}

/* Runs in the probe, so the record holds the lock. A child asks the OS who
   holds a write lock on the lock file. */
typedef struct {
    Probe base;
    const char *lock;
    int holder_seen;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} LockProbe;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the BrainProbe callback signature
static int probe_lock_held(void *ctx, const char *path, const char *brain_id)
{
    LockProbe *p = (LockProbe *)ctx;
    pid_t child = 0;
    int status = 0;

    (void)path;
    (void)brain_id;
    child = fork();
    if (child == 0) {
        struct flock query = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
        int fd = open(p->lock, O_RDWR | O_CLOEXEC);

        if (fd < 0 || fcntl(fd, F_GETLK, &query) != 0) {
            _exit(2);
        }
        _exit(query.l_type == F_WRLCK ? 0 : 1);
    }
    ASSERT_TRUE(child > 0);
    ASSERT_EQ_INT(waitpid(child, &status, 0), (long long)child);
    p->holder_seen = (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 1 : 0;
    return 0;
}

TEST(brain_list_record_holds_the_advisory_lock_while_it_decides)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    char *lock = given_path(dir, "brains.json.lock");
    LockProbe probe_ctx = {.lock = lock};
    BrainProbe probe = {.still = probe_lock_held, .mint = probe_mint, .ctx = &probe_ctx};
    char minted[ID_CAP];

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/b.db", ID_A, &probe, minted), BRAIN_LIST_MOVED);
    ASSERT_EQ_INT(probe_ctx.holder_seen, 1);
    free(lock);
    free(file);
    free(dir);
}

TEST(brain_list_a_copy_takes_the_minted_id_and_keeps_the_original_row)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe still = {.still = 1};
    BrainProbe probe = given_probe(&still);
    char minted[ID_CAP];
    BrainList list = {0};

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/copy.db", ID_A, &probe, minted), BRAIN_LIST_CLONED);
    ASSERT_STREQ(minted, ID_MINTED);
    ASSERT_EQ_INT(still.mints, 1);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 2);
    ASSERT_STREQ(row_at(&list, 0)->path, "/tmp/a.db");
    ASSERT_STREQ(row_at(&list, 0)->brain_id, ID_A);
    ASSERT_STREQ(row_at(&list, 1)->path, "/tmp/copy.db");
    ASSERT_STREQ(row_at(&list, 1)->brain_id, ID_MINTED);
    ASSERT_STREQ(row_at(&list, 1)->added_at, LATER);
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_a_copy_whose_path_is_listed_gets_the_minted_id_in_that_row)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe still = {.still = 1};
    BrainProbe probe = given_probe(&still);
    char minted[ID_CAP];
    BrainList list = {0};
    const char *paths[] = {"/tmp/a.db", "/tmp/copy.db"};
    const char *ids[] = {ID_A, ID_B};

    given_rows(file, paths, ids, 2U);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/copy.db", ID_A, &probe, minted), BRAIN_LIST_CLONED);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 2);
    ASSERT_STREQ(row_at(&list, 1)->path, "/tmp/copy.db");
    ASSERT_STREQ(row_at(&list, 1)->brain_id, ID_MINTED);
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_a_copy_whose_mint_fails_changes_nothing)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe still = {.still = 1, .mint_fails = 1};
    BrainProbe probe = given_probe(&still);
    char minted[ID_CAP];
    char *text = NULL;

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/copy.db", ID_A, &probe, minted), BRAIN_LIST_IO);
    text = file_text(file);
    ASSERT_STREQ(text, k_doc_one);
    free(text);
    free(file);
    free(dir);
}

TEST(brain_list_a_plain_open_mints_nothing)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];

    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_ADDED);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/b.db", ID_A, &probe, minted), BRAIN_LIST_MOVED);
    ASSERT_EQ_INT(never.mints, 0);
    free(file);
    free(dir);
}

TEST(brain_list_a_move_keeps_the_id_and_changes_the_path)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    BrainList list = {0};

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/moved.db", ID_A, &probe, minted), BRAIN_LIST_MOVED);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, "/tmp/moved.db");
    ASSERT_STREQ(row_at(&list, 0)->brain_id, ID_A);
    ASSERT_STREQ(row_at(&list, 0)->added_at, NOW);
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_a_move_onto_a_listed_path_drops_the_stale_row_of_that_path)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    BrainList list = {0};
    const char *paths[] = {"/tmp/a.db", "/tmp/new.db"};
    const char *ids[] = {ID_A, ID_B};

    given_rows(file, paths, ids, 2U);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/new.db", ID_A, &probe, minted), BRAIN_LIST_MOVED);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, "/tmp/new.db");
    ASSERT_STREQ(row_at(&list, 0)->brain_id, ID_A);
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_a_listed_path_with_a_new_id_takes_that_id)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    BrainList list = {0};

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_B, &probe, minted), BRAIN_LIST_UPDATED);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, ID_B);
    ASSERT_STREQ(row_at(&list, 0)->added_at, NOW);
    brain_list_free(&list);
    free(file);
    free(dir);
}

/* G2 A: the other path could not be checked, so nothing is minted and nothing is written. */
TEST(brain_list_a_probe_that_cannot_tell_writes_nothing_and_mints_nothing)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    struct stat before;
    struct stat after;
    Probe unsure = {.still = -1};
    BrainProbe probe = given_probe(&unsure);
    char minted[ID_CAP];

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_INT(stat(file, &before), 0);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/b.db", ID_A, &probe, minted), BRAIN_LIST_UNDECIDED);
    ASSERT_EQ_INT(stat(file, &after), 0);
    ASSERT_TRUE(after.st_ino == before.st_ino);
    ASSERT_EQ_INT(unsure.mints, 0);
    free(file);
    free(dir);
}

TEST(brain_list_read_of_a_missing_file_is_an_empty_list)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 0);
    brain_list_free(&list);
    ASSERT_TRUE(access(file, F_OK) != 0);
    free(file);
    free(dir);
}

static bool missing_gone(void *ctx, const char *path)
{
    (void)ctx;
    return strcmp(path, "/tmp/gone.db") == 0;
}

TEST(brain_list_a_record_that_changes_nothing_leaves_the_file_as_it_is)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    struct stat before;
    struct stat after;
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_INT(stat(file, &before), 0);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT(stat(file, &after), 0);
    ASSERT_TRUE(after.st_ino == before.st_ino);
    free(file);
    free(dir);
}

TEST(brain_list_a_folder_that_cannot_take_the_temp_file_returns_io_and_keeps_the_file)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    char *text = NULL;

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_INT(chmod(dir, 0500), 0);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/b.db", ID_B, &probe, minted), BRAIN_LIST_IO);
    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, NULL, NULL),
                     BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT(chmod(dir, 0700), 0);
    text = file_text(file);
    ASSERT_STREQ(text, k_doc_one);
    free(text);
    free(file);
    free(dir);
}

TEST(brain_list_prune_drops_a_missing_path_and_returns_its_row)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    const char *paths[] = {"/tmp/gone.db", "/tmp/keep.db"};
    const char *ids[] = {ID_A, ID_B};
    BrainList list = {0};
    BrainList removed = {0};

    given_rows(file, paths, ids, 2U);
    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, &removed, NULL),
                     BRAIN_LIST_PRUNED);
    ASSERT_EQ_INT((long long)removed.count, 1);
    ASSERT_STREQ(row_at(&removed, 0)->path, "/tmp/gone.db");
    ASSERT_STREQ(row_at(&removed, 0)->brain_id, ID_A);
    brain_list_free(&removed);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, "/tmp/keep.db");
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_prune_with_nothing_missing_does_not_rewrite_the_file)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    struct stat before;
    struct stat after;
    BrainList removed = {0};

    given_one_row(file, "/tmp/keep.db", ID_B);
    ASSERT_EQ_INT(stat(file, &before), 0);
    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, &removed, NULL),
                     BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)removed.count, 0);
    ASSERT_EQ_INT(stat(file, &after), 0);
    ASSERT_TRUE(after.st_ino == before.st_ino);
    brain_list_free(&removed);
    free(file);
    free(dir);
}

TEST(brain_list_prune_of_a_missing_file_creates_nothing)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");

    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, NULL, NULL),
                     BRAIN_LIST_UNCHANGED);
    ASSERT_TRUE(access(file, F_OK) != 0);
    free(file);
    free(dir);
}

TEST(brain_list_calls_without_a_path_or_a_probe_fail_without_writing)
{
    char minted[ID_CAP];
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    BrainList list = {0};

    ASSERT_EQ_STATUS(
        brain_list_record(NULL,
                          (BrainRecordIn){.real_path = "/tmp/a.db", .brain_id = ID_A, .now = NOW},
                          &probe, (BrainRecordOut){.minted = minted, .minted_cap = sizeof(minted)}),
        BRAIN_LIST_IO);
    ASSERT_EQ_STATUS(
        brain_list_record("/tmp/x/brains.json",
                          (BrainRecordIn){.real_path = "/tmp/a.db", .brain_id = ID_A, .now = NOW},
                          NULL, (BrainRecordOut){.minted = minted, .minted_cap = sizeof(minted)}),
        BRAIN_LIST_IO);
    ASSERT_EQ_STATUS(brain_list_read(NULL, &list), BRAIN_LIST_IO);
    ASSERT_EQ_STATUS(brain_list_prune(NULL, missing_gone, NULL, 0, NULL, NULL), BRAIN_LIST_IO);
    ASSERT_EQ_STATUS(brain_list_prune("/tmp/x/brains.json", NULL, NULL, 0, NULL, NULL),
                     BRAIN_LIST_IO);
}

/* ---- brains.json is hand-editable and another program's input -------------- */

static const char k_head[] = "{\"format\":\"remember-brains\",\"version\":1,\"brains\":";

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) two names of one kind, read in order
static void given_json(const char *file, const char *brains_array)
{
    char text[TEXT_CAP];

    ASSERT_TRUE(snprintf(text, sizeof(text), "%s%s}", k_head, brains_array) < (int)sizeof(text));
    given_text(file, text);
}

TEST(brain_list_a_nul_inside_a_path_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_json(file, "[{\"path\":\"/tmp/a\\u0000/evil\",\"brain_id\":\"x\",\"added_at\":\"y\"}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    ASSERT_EQ_INT((long long)list.count, 0);
    free(file);
    free(dir);
}

TEST(brain_list_a_nul_inside_a_brain_id_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_json(file, "[{\"path\":\"/tmp/a\",\"brain_id\":\"x\\u0000y\",\"added_at\":\"y\"}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

TEST(brain_list_a_raw_nul_byte_in_the_file_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};
    static const char doc[] =
        "{\"format\":\"remember-brains\",\"version\":1,\"brains\":[]}\0{\"extra\":1}";

    given_bytes(file, doc, sizeof(doc) - 1U);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

TEST(brain_list_a_row_with_a_wrong_type_or_a_missing_field_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_json(file, "[{\"path\":1,\"brain_id\":\"x\",\"added_at\":\"y\"}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_json(file, "[{\"path\":\"/a\",\"brain_id\":\"x\"}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_json(file, "[\"/a\"]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

TEST(brain_list_a_document_of_another_shape_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_text(file, "[]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "[\"format\",\"version\",\"brains\"]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "{\"format\":\"other\",\"version\":1,\"brains\":[]}");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "{\"format\":\"remember-brains\",\"version\":2,\"brains\":[]}");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "{\"format\":\"remember-brains\",\"version\":1,\"brains\":{}}");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "{\"format\":\"remember-brains\",\"version\":1");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

/* A member given twice is read first-wins here and last-wins by most other readers, so the
   path remember checks would not be the path a reviewer of the file sees (plan 25 X3). */
TEST(brain_list_a_row_field_given_twice_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_json(file, "[{\"path\":\"/nope/a\",\"path\":\"/etc/hosts\",\"brain_id\":\"x\","
                     "\"added_at\":\"y\"}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_json(file,
               "[{\"path\":\"/a\",\"brain_id\":\"x\",\"brain_id\":\"z\",\"added_at\":\"y\"}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_json(file,
               "[{\"path\":\"/a\",\"brain_id\":\"x\",\"added_at\":\"y\",\"added_at\":\"z\"}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

TEST(brain_list_a_document_member_given_twice_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_text(file, "{\"format\":\"remember-brains\",\"version\":1,\"brains\":[],"
                     "\"brains\":[{\"path\":\"/a\",\"brain_id\":\"x\",\"added_at\":\"y\"}]}");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "{\"format\":\"remember-brains\",\"format\":\"other\",\"version\":1,"
                     "\"brains\":[]}");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    given_text(file, "{\"format\":\"remember-brains\",\"version\":1,\"version\":2,\"brains\":[]}");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

/* A member the reader does not take may repeat: it is ignored either way. */
TEST(brain_list_an_unknown_member_given_twice_is_read)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_json(file, "[{\"path\":\"/a\",\"brain_id\":\"x\",\"added_at\":\"y\",\"n\":1,\"n\":2}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_read_names_the_first_unknown_member_root_first)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_text(file, "{\"format\":\"remember-brains\",\"version\":1,\"z\":0,\"brains\":"
                     "[{\"path\":\"/a\",\"brain_id\":\"x\",\"added_at\":\"y\",\"r\":1}]}");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_STREQ(list.unknown_field != NULL ? list.unknown_field : "(none)", "z");
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_read_names_an_unknown_member_of_a_later_row)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_json(file, "[{\"path\":\"/a\",\"brain_id\":\"x\",\"added_at\":\"y\"},"
                     "{\"path\":\"/b\",\"brain_id\":\"w\",\"added_at\":\"y\",\"r2\":1}]");
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_STREQ(list.unknown_field != NULL ? list.unknown_field : "(none)", "r2");
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_read_of_a_file_with_only_known_members_names_none)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_TRUE(list.unknown_field == NULL);
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_record_and_prune_hand_the_unknown_member_to_the_caller)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    char *unknown = NULL;

    given_json(file, "[{\"path\":\"/a\",\"brain_id\":\"x\",\"added_at\":\"y\",\"r\":1}]");
    ASSERT_EQ_STATUS(
        brain_list_record(
            file, (BrainRecordIn){.real_path = "/a", .brain_id = "x", .now = NOW}, &probe,
            (BrainRecordOut){
                .minted = minted, .minted_cap = sizeof(minted), .unknown_field = &unknown}),
        BRAIN_LIST_UNCHANGED);
    ASSERT_STREQ(unknown != NULL ? unknown : "(none)", "r");
    free(unknown);
    unknown = NULL;
    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, NULL, &unknown),
                     BRAIN_LIST_UNCHANGED);
    ASSERT_STREQ(unknown != NULL ? unknown : "(none)", "r");
    free(unknown);
    free(file);
    free(dir);
}

/* Copy piece and its NUL to text + *used inside MANY_CAP, or fail the fixture. The next piece
   starts on that NUL. */
static void append_text(char *text, size_t *used, const char *piece)
{
    size_t n = strlen(piece);

    if (n >= (size_t)MANY_CAP - *used) {
        fixture_failed("text too long");
    }
    memcpy(text + *used, piece, n + 1U);
    *used += n;
}

TEST(brain_list_has_no_row_cap)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};
    char *text = (char *)malloc(MANY_CAP);
    size_t used = 0;
    int row = 0;

    if (text == NULL) {
        fixture_failed("rows text");
    }
    append_text(text, &used, k_head);
    append_text(text, &used, "[");
    for (row = 0; row < MANY_ROWS; row++) {
        char one[SMALL_ROW];
        int n = snprintf(one, sizeof(one),
                         "%s{\"path\":\"/tmp/b%d.db\",\"brain_id\":\"i%d\",\"added_at\":\"t\"}",
                         row > 0 ? "," : "", row, row);

        if (n < 0 || (size_t)n >= sizeof(one)) {
            fixture_failed("row text");
        }
        append_text(text, &used, one);
    }
    append_text(text, &used, "]}");
    given_bytes(file, text, used);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, MANY_ROWS);
    brain_list_free(&list);
    free(text);
    free(file);
    free(dir);
}

/* A valid document padded with trailing spaces to total bytes. */
static void given_padded_doc(const char *file, size_t total)
{
    char *text = (char *)malloc(total);
    char doc[TEXT_CAP];
    int used = snprintf(doc, sizeof(doc), "%s[]}", k_head);

    if (text == NULL) {
        fixture_failed("padded text");
    }
    memset(text, ' ', total);
    memcpy(text, doc, (size_t)used);
    given_bytes(file, text, total);
    free(text);
}

TEST(brain_list_a_file_of_exactly_the_cap_is_read)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_padded_doc(file, (size_t)BRAIN_LIST_FILE_MAX);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    free(file);
    free(dir);
}

TEST(brain_list_a_file_one_byte_over_the_cap_is_unreadable_and_left_alone)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    struct stat st;

    given_padded_doc(file, (size_t)BRAIN_LIST_FILE_MAX + 1U);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_UNREADABLE);
    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, NULL, NULL),
                     BRAIN_LIST_UNREADABLE);
    ASSERT_EQ_INT(stat(file, &st), 0);
    ASSERT_EQ_INT((long long)st.st_size, (long long)BRAIN_LIST_FILE_MAX + 1);
    free(file);
    free(dir);
}

TEST(brain_list_an_unreadable_file_is_never_written_over)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    char *text = NULL;

    given_text(file, "not json at all");
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_UNREADABLE);
    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, NULL, NULL),
                     BRAIN_LIST_UNREADABLE);
    text = file_text(file);
    ASSERT_STREQ(text, "not json at all");
    free(text);
    free(file);
    free(dir);
}

TEST(brain_list_a_fifo_at_brains_json_is_unreadable_and_does_not_block)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    ASSERT_EQ_INT(mkfifo(file, DB_FILE_PERMS), 0);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

TEST(brain_list_a_folder_at_brains_json_is_unreadable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    ASSERT_EQ_INT(mkdir(file, 0700), 0);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    free(file);
    free(dir);
}

TEST(brain_list_a_symlink_at_brains_json_to_a_regular_file_is_read)
{
    char *dir = given_dir();
    char *real = given_path(dir, "real.json");
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};

    given_one_row(real, "/tmp/a.db", ID_A);
    ASSERT_EQ_INT(symlink(real, file), 0);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    brain_list_free(&list);
    free(file);
    free(real);
    free(dir);
}

TEST(brain_list_a_symlink_at_the_lock_is_not_followed)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    char *lock = given_path(dir, "brains.json.lock");
    char *victim = given_path(dir, "victim");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];

    ASSERT_EQ_INT(symlink(victim, lock), 0);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_IO);
    ASSERT_TRUE(access(victim, F_OK) != 0);
    ASSERT_TRUE(access(file, F_OK) != 0);
    ASSERT_EQ_STATUS(brain_list_prune(file, missing_gone, NULL, 0, NULL, NULL), BRAIN_LIST_IO);
    free(victim);
    free(lock);
    free(file);
    free(dir);
}

TEST(brain_list_a_lock_that_cannot_be_made_leaves_the_previous_file)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    char *lock = given_path(dir, "brains.json.lock");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    char *text = NULL;

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_INT(unlink(lock), 0);
    ASSERT_EQ_INT(mkdir(lock, 0700), 0);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/b.db", ID_B, &probe, minted), BRAIN_LIST_IO);
    text = file_text(file);
    ASSERT_STREQ(text, k_doc_one);
    free(text);
    free(lock);
    free(file);
    free(dir);
}

/* ---- every allocation can fail, and the file is then whole ------------------ */

typedef enum {
    CASE_ADD,
    CASE_COPY,
    CASE_COPY_LISTED,
    CASE_MOVE,
    CASE_UPDATE,
    CASE_PRUNE,
    CASE_READ
} Case;

static BrainListCode run_case(Case which, const char *file)
{
    Probe still = {.still = 1};
    Probe never = {.still = 0};
    BrainProbe copy_probe = given_probe(&still);
    BrainProbe move_probe = given_probe(&never);
    char minted[ID_CAP];
    BrainList list = {0};
    BrainListCode code = BRAIN_LIST_UNCHANGED;

    switch (which) {
    case CASE_ADD:
        return record_at(file, "/tmp/new.db", ID_MINTED, &move_probe, minted);
    case CASE_COPY:
        return record_at(file, "/tmp/copy.db", ID_A, &copy_probe, minted);
    case CASE_COPY_LISTED:
        return record_at(file, "/tmp/b.db", ID_A, &copy_probe, minted);
    case CASE_MOVE:
        return record_at(file, "/tmp/b.db", ID_A, &move_probe, minted);
    case CASE_UPDATE:
        return record_at(file, "/tmp/a.db", ID_MINTED, &move_probe, minted);
    case CASE_PRUNE:
        return brain_list_prune(file, missing_gone, NULL, 0, NULL, NULL);
    case CASE_READ:
    default:
        break;
    }
    code = brain_list_read(file, &list);
    brain_list_free(&list);
    return code;
}

/* For one case: a clean run gives the file it should end with. Then the n-th
   allocation fails, for every n, once in this module and once in the JSON reader
   (the shared counter). The call returns what a clean run returns or
   BRAIN_LIST_IO, and the file is the old one or the new one, never a mix. */
static void sweep_allocations(Case which, BrainListCode want)
{
    enum { SWEEP = 90 };
    void (*const arm[])(int) = {brain_list_test_fail_alloc_after, test_alloc_fail_after};
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    const char *paths[] = {"/tmp/a.db", "/tmp/gone.db"};
    const char *ids[] = {ID_A, ID_B};
    char *before = NULL;
    char *after = NULL;
    size_t counter = 0;

    given_rows(file, paths, ids, 2U);
    before = file_text(file);
    ASSERT_EQ_STATUS(run_case(which, file), want);
    after = file_text(file);
    for (counter = 0; counter < sizeof(arm) / sizeof(arm[0]); counter++) {
        int n = 0;

        for (n = 0; n < SWEEP; n++) {
            BrainListCode code = BRAIN_LIST_UNCHANGED;
            char *text = NULL;

            given_text(file, before);
            arm[counter](n);
            code = run_case(which, file);
            arm[counter](-1);
            text = file_text(file);
            ASSERT_TRUE(code == want || code == BRAIN_LIST_IO);
            ASSERT_STREQ(text, code == want ? after : before);
            free(text);
        }
    }
    free(after);
    free(before);
    free(file);
    free(dir);
}

TEST(brain_list_add_survives_each_allocation_failure)
{
    sweep_allocations(CASE_ADD, BRAIN_LIST_ADDED);
}

TEST(brain_list_a_copy_survives_each_allocation_failure)
{
    sweep_allocations(CASE_COPY, BRAIN_LIST_CLONED);
}

TEST(brain_list_a_copy_of_a_listed_path_survives_each_allocation_failure)
{
    sweep_allocations(CASE_COPY_LISTED, BRAIN_LIST_CLONED);
}

TEST(brain_list_a_move_survives_each_allocation_failure)
{
    sweep_allocations(CASE_MOVE, BRAIN_LIST_MOVED);
}

TEST(brain_list_an_id_update_survives_each_allocation_failure)
{
    sweep_allocations(CASE_UPDATE, BRAIN_LIST_UPDATED);
}

TEST(brain_list_prune_survives_each_allocation_failure)
{
    sweep_allocations(CASE_PRUNE, BRAIN_LIST_PRUNED);
}

TEST(brain_list_read_survives_each_allocation_failure)
{
    sweep_allocations(CASE_READ, BRAIN_LIST_UNCHANGED);
}

TEST(brain_list_a_fifo_at_brains_json_is_not_read_even_when_it_holds_a_whole_document)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};
    int reader = -1;
    int writer = -1;

    ASSERT_EQ_INT(mkfifo(file, DB_FILE_PERMS), 0);
    /* The reader end stays open, so the document waits in the pipe and, with the writer
       closed, a read of the pipe ends at its last byte as a regular file would. */
    reader = open(file, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    writer = open(file, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    ASSERT_TRUE(reader >= 0 && writer >= 0);
    ASSERT_EQ_INT((long long)write(writer, k_doc_one, sizeof(k_doc_one) - 1U),
                  (long long)(sizeof(k_doc_one) - 1U));
    ASSERT_EQ_INT(close(writer), 0);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNREADABLE);
    ASSERT_EQ_INT(close(reader), 0);
    free(file);
    free(dir);
}

TEST(brain_list_eight_concurrent_records_all_keep_their_row)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    BrainList list = {0};
    pid_t kids[CHILDREN];
    int i = 0;

    for (i = 0; i < CHILDREN; i++) {
        kids[i] = fork();
        if (kids[i] == 0) {
            Probe never = {.still = 0};
            BrainProbe probe = given_probe(&never);
            char minted[ID_CAP];
            char path[ID_CAP];
            char id[ID_CAP];

            (void)snprintf(path, sizeof(path), "/tmp/c%d.db", i);
            (void)snprintf(id, sizeof(id), "01a0d90a-e71c-7000-8000-00000000010%d", i);
            _exit(record_at(file, path, id, &probe, minted) == BRAIN_LIST_ADDED ? 0 : 1);
        }
        ASSERT_TRUE(kids[i] > 0);
    }
    for (i = 0; i < CHILDREN; i++) {
        int status = 0;

        ASSERT_EQ_INT(waitpid(kids[i], &status, 0), (long long)kids[i]);
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, CHILDREN);
    brain_list_free(&list);
    free(file);
    free(dir);
}

/* ---- black box: the CLI records every brain it opens ----------------------- */

typedef struct {
    char *saved;
    const char *name;
    bool had;
    char pad_[ENV_PAD]; /* explicit tail padding (kept -Wpadded-clean) */
} EnvHold;

/* Sets name to value, or unsets it when value is NULL; env_release puts it back. */
static EnvHold env_hold(const char *name, const char *value)
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
    const char *old = getenv(name);
    EnvHold hold = {.name = name, .had = old != NULL};

    if (old != NULL) {
        hold.saved = strdup(old);
        ASSERT_TRUE(hold.saved != NULL);
    }
    if (value != NULL) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
        ASSERT_EQ_INT(setenv(name, value, 1), 0);
    } else {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
        ASSERT_EQ_INT(unsetenv(name), 0);
    }
    return hold;
}

static void env_release(EnvHold *hold)
{
    if (hold->had) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
        ASSERT_EQ_INT(setenv(hold->name, hold->saved, 1), 0);
    } else {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
        ASSERT_EQ_INT(unsetenv(hold->name), 0);
    }
    free(hold->saved);
    hold->saved = NULL;
}

/* One folder with its own data dir: XDG_DATA_HOME points into it, so no test
   touches the real ~/.remember. */
typedef struct {
    char *dir;
    char *list;  /* <dir>/xdg/remember/brains.json */
    char *brain; /* <dir>/one.db */
    char *other; /* <dir>/two.db */
    EnvHold xdg;
    EnvHold user_db;
} Scene;

static Scene given_scene(void)
{
    Scene scn = {.dir = given_dir()};
    char *xdg = given_path(scn.dir, "xdg");

    scn.list = given_path(scn.dir, "xdg/remember/brains.json");
    scn.brain = given_path(scn.dir, "one.db");
    scn.other = given_path(scn.dir, "two.db");
    scn.xdg = env_hold("XDG_DATA_HOME", xdg);
    scn.user_db = env_hold("REMEMBER_USER_DB", NULL);
    free(xdg);
    return scn;
}

static void scene_release(Scene *scn)
{
    env_release(&scn->user_db);
    env_release(&scn->xdg);
    free(scn->other);
    free(scn->brain);
    free(scn->list);
    free(scn->dir);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) two names of one kind, read in order
static CmdResult run_args(const char *db, const char *a, const char *b, const char *c)
{
    const char *args[3];
    size_t n = 0;

    args[n++] = a;
    if (b != NULL) {
        args[n++] = b;
    }
    if (c != NULL) {
        args[n++] = c;
    }
    return run_remember(db, args, n, NULL);
}

/* Runs `add hello` on db; the exit must be 0. */
static void given_brain_opened(const char *db)
{
    CmdResult r = run_args(db, "add", "hello", NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

__attribute__((returns_nonnull)) static char *real_path_of(const char *path)
{
    char *out = realpath(path, NULL);

    if (out == NULL) {
        fixture_failed("realpath");
    }
    return out;
}

__attribute__((returns_nonnull)) static char *brain_id_in(const char *db)
{
    char *id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");

    if (id == NULL) {
        fixture_failed("brain id");
    }
    return id;
}

TEST(opening_a_brain_read_write_adds_one_row_and_a_second_open_adds_none)
{
    Scene scn = given_scene();
    BrainList list = {0};
    char *real = NULL;
    char *id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    r = run_args(scn.brain, "list", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    real = real_path_of(scn.brain);
    id = brain_id_in(scn.brain);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, real);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, id);
    ASSERT_EQ_INT((long long)strlen(row_at(&list, 0)->added_at), 24);
    brain_list_free(&list);
    free(id);
    free(real);
    scene_release(&scn);
}

TEST(brains_json_follows_the_data_dir_and_not_the_user_db)
{
    Scene scn = given_scene();
    char *elsewhere = given_path(scn.dir, "elsewhere");
    char *user_db = given_path(scn.dir, "elsewhere/user.db");
    char *beside = given_path(scn.dir, "elsewhere/brains.json");
    EnvHold hold = env_hold("REMEMBER_USER_DB", user_db);

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(access(scn.list, F_OK), 0);
    ASSERT_TRUE(access(beside, F_OK) != 0);
    env_release(&hold);
    free(beside);
    free(user_db);
    free(elsewhere);
    scene_release(&scn);
}

TEST(brains_json_is_in_home_dot_remember_when_xdg_data_home_is_unset)
{
    char *dir = given_dir();
    char *brain = given_path(dir, "one.db");
    char *list = given_path(dir, ".remember/brains.json");
    EnvHold xdg = env_hold("XDG_DATA_HOME", NULL);
    EnvHold home = env_hold("HOME", dir);
    EnvHold user_db = env_hold("REMEMBER_USER_DB", NULL);

    given_brain_opened(brain);
    ASSERT_EQ_INT(access(list, F_OK), 0);
    env_release(&user_db);
    env_release(&home);
    env_release(&xdg);
    free(list);
    free(brain);
    free(dir);
}

TEST(the_data_dir_is_made_with_mode_0700_when_it_is_missing)
{
    Scene scn = given_scene();
    char *folder = given_path(scn.dir, "xdg/remember");
    struct stat st;

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(stat(folder, &st), 0);
    ASSERT_EQ_INT((unsigned)st.st_mode & (unsigned)PERM_BITS_MASK, DIR_PERMS);
    free(folder);
    scene_release(&scn);
}

static void copy_file_to(const char *from, const char *to)
{
    ASSERT_EQ_INT(harness_copy_file(from, to), 0);
}

TEST(a_copied_brain_gets_a_new_id_and_prints_the_c38_line_with_its_fix)
{
    Scene scn = given_scene();
    char *real = NULL;
    char *old_id = NULL;
    char *new_id = NULL;
    char want[PATH_CAP * 2];
    CmdResult r;
    BrainList list = {0};

    given_brain_opened(scn.brain);
    old_id = brain_id_in(scn.brain);
    copy_file_to(scn.brain, scn.other);
    r = run_args(scn.other, "tags", NULL, NULL);
    real = real_path_of(scn.other);
    new_id = brain_id_in(scn.other);
    ASSERT_TRUE(snprintf(want, sizeof(want),
                         "remember: warning: brain \"%s\" looks like a copy of another brain, so "
                         "it got its own new id, %s. The original keeps the old id. Both now sync "
                         "and run hooks as separate brains. Run 'remember brain list' to see "
                         "both.\n",
                         real, new_id) < (int)sizeof(want));
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, want);
    ASSERT_TRUE(strcmp(old_id, new_id) != 0);
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 2);
    ASSERT_STREQ(row_at(&list, 1)->brain_id, new_id);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, old_id);
    brain_list_free(&list);
    free(new_id);
    free(old_id);
    free(real);
    scene_release(&scn);
}

TEST(a_copied_brain_warns_once_and_json_carries_message_and_fix)
{
    Scene scn = given_scene();
    const char *json[] = {"--json", "tags"};
    CmdResult r;

    given_brain_opened(scn.brain);
    copy_file_to(scn.brain, scn.other);
    r = run_remember(scn.other, json, 2U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out,
                        "\"warnings\":[{\"code\":\"brain_id_cloned\",\"message\":\"brain \\\"");
    ASSERT_STR_CONTAINS(r.out, "Both now sync and run hooks as separate brains.\","
                               "\"fix\":\"Run 'remember brain list' to see both.\"}]");
    cmd_result_free(&r);
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    scene_release(&scn);
}

TEST(a_moved_brain_keeps_its_id_updates_the_path_and_warns_nothing)
{
    Scene scn = given_scene();
    BrainList list = {0};
    char *real = NULL;
    char *id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    id = brain_id_in(scn.brain);
    ASSERT_EQ_INT(rename(scn.brain, scn.other), 0);
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    real = real_path_of(scn.other);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, real);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, id);
    brain_list_free(&list);
    free(real);
    free(id);
    scene_release(&scn);
}

TEST(brain_new_id_is_recorded_on_the_next_open_without_a_second_row)
{
    Scene scn = given_scene();
    BrainList list = {0};
    char *id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    r = run_args(scn.brain, "brain", "new-id", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_args(scn.brain, "tags", NULL, NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    id = brain_id_in(scn.brain);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, id);
    brain_list_free(&list);
    free(id);
    scene_release(&scn);
}

/* Plan 26 bug: new-id left brains.json on the old id, so the copy of the new brain was not
   flagged and the original was, on its next open. The copy must be the one that is flagged. */
TEST(brain_new_id_updates_brains_json_so_a_copy_of_the_new_id_is_the_one_flagged)
{
    Scene scn = given_scene();
    BrainList list = {0};
    char *id = NULL;
    char *copy_id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    r = run_args(scn.brain, "brain", "new-id", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    id = brain_id_in(scn.brain);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, id);
    brain_list_free(&list);
    copy_file_to(scn.brain, scn.other);
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "looks like a copy of another brain");
    cmd_result_free(&r);
    copy_id = brain_id_in(scn.other);
    ASSERT_TRUE(strcmp(copy_id, id) != 0);
    r = run_args(scn.brain, "tags", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(copy_id);
    copy_id = brain_id_in(scn.brain);
    ASSERT_STREQ(copy_id, id);
    free(copy_id);
    free(id);
    scene_release(&scn);
}

TEST(brain_list_prints_path_id_and_added_at_per_row_and_a_path_with_no_brain_prints_the_c01_line)
{
    Scene scn = given_scene();
    char *real_one = NULL;
    char *real_two = NULL;
    char want_out[PATH_CAP * 2];
    char want_err[PATH_CAP * 2];
    CmdResult r;
    BrainList list = {0};

    given_brain_opened(scn.brain);
    given_brain_opened(scn.other);
    real_one = real_path_of(scn.brain);
    real_two = real_path_of(scn.other);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_TRUE(snprintf(want_out, sizeof(want_out), "%s\t%s\t%s\n%s\t%s\t%s\n", real_one,
                         row_at(&list, 0)->brain_id, row_at(&list, 0)->added_at, real_two,
                         row_at(&list, 1)->brain_id,
                         row_at(&list, 1)->added_at) < (int)sizeof(want_out));
    brain_list_free(&list);
    ASSERT_EQ_INT(unlink(scn.other), 0);
    r = run_args(scn.brain, "brain", "list", NULL);
    ASSERT_TRUE(snprintf(want_err, sizeof(want_err),
                         "remember: warning: brain \"%s\" is in the list but isn't on disk. If you "
                         "moved it, open it once from its new place. Otherwise run 'remember brain "
                         "prune' to remove it from the list.\n",
                         real_two) < (int)sizeof(want_err));
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, want_out);
    ASSERT_STREQ(r.err, want_err);
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 2);
    brain_list_free(&list);
    free(real_two);
    free(real_one);
    scene_release(&scn);
}

TEST(brain_list_json_gives_the_rows_and_a_brain_missing_warning_with_its_fix)
{
    Scene scn = given_scene();
    const char *json[] = {"--json", "brain", "list"};
    CmdResult r;

    given_brain_opened(scn.brain);
    given_brain_opened(scn.other);
    ASSERT_EQ_INT(unlink(scn.other), 0);
    r = run_remember(scn.brain, json, 3U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,"
                             "\"type\":\"brain.list.result\",\"data\":{\"count\":2,\"brains\":[{"
                             "\"path\":\"");
    ASSERT_STR_CONTAINS(r.out, "\"brain_id\":\"");
    ASSERT_STR_CONTAINS(r.out, "\"added_at\":\"");
    ASSERT_STR_CONTAINS(r.out,
                        "\"warnings\":[{\"code\":\"brain_missing\",\"message\":\"brain \\\"");
    ASSERT_STR_CONTAINS(r.out, "is in the list but isn't on disk.\",\"fix\":\"If you moved it, "
                               "open it once from its new place. Otherwise run 'remember brain "
                               "prune' to remove it from the list.\"}]");
    cmd_result_free(&r);
    scene_release(&scn);
}

TEST(brain_list_with_no_file_prints_nothing_and_creates_no_brain)
{
    Scene scn = given_scene();
    CmdResult r = run_args(scn.brain, "brain", "list", NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "");
    ASSERT_TRUE(access(scn.brain, F_OK) != 0);
    ASSERT_TRUE(access(scn.list, F_OK) != 0);
    cmd_result_free(&r);
    scene_release(&scn);
}

TEST(brain_prune_drops_a_path_with_no_brain_and_keeps_one_that_has_it)
{
    Scene scn = given_scene();
    BrainList list = {0};
    char *real_one = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    given_brain_opened(scn.other);
    real_one = real_path_of(scn.brain);
    ASSERT_EQ_INT(unlink(scn.other), 0);
    r = run_args(scn.brain, "brain", "prune", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, real_one);
    brain_list_free(&list);
    free(real_one);
    scene_release(&scn);
}

TEST(brain_prune_json_names_the_dropped_rows)
{
    Scene scn = given_scene();
    const char *json[] = {"--json", "brain", "prune"};
    CmdResult r;

    given_brain_opened(scn.brain);
    given_brain_opened(scn.other);
    ASSERT_EQ_INT(unlink(scn.other), 0);
    r = run_remember(scn.brain, json, 3U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,"
                             "\"type\":\"brain.prune.result\",\"data\":{\"action\":\"pruned\","
                             "\"count\":1,\"brains\":[{\"path\":\"");
    cmd_result_free(&r);
    scene_release(&scn);
}

#ifdef REMEMBER_TEST_VFS
/* The test CLI fails the rename of the temp file (REMEMBER_TEST_EXPORT_FAIL). */
TEST(a_failed_replace_leaves_the_previous_file_and_the_open_exits_as_before)
{
    Scene scn = given_scene();
    char *before = NULL;
    char *after = NULL;
    char *tmp = given_path(scn.dir, "xdg/remember/brains.json.tmp");
    EnvHold fail = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    before = file_text(scn.list);
    fail = env_hold("REMEMBER_TEST_EXPORT_FAIL", "rename");
    r = run_args(scn.other, "add", "second", NULL);
    env_release(&fail);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    after = file_text(scn.list);
    ASSERT_STREQ(after, before);
    ASSERT_TRUE(access(tmp, F_OK) != 0);
    free(after);
    free(before);
    free(tmp);
    scene_release(&scn);
}
#endif

TEST(a_fifo_at_a_listed_brain_path_does_not_block_a_later_open)
{
    Scene scn = given_scene();
    char *real = NULL;
    char *id = NULL;
    char *file = NULL;
    char *fifo = given_path(scn.dir, "fifo.db");
    CmdResult r;

    given_brain_opened(scn.brain);
    real = real_path_of(scn.brain);
    id = brain_id_in(scn.brain);
    ASSERT_EQ_INT(unlink(scn.brain), 0);
    ASSERT_EQ_INT(mkfifo(fifo, DB_FILE_PERMS), 0);
    file = real_path_of(fifo);
    {
        char doc[PATH_CAP * 2];

        ASSERT_TRUE(snprintf(doc, sizeof(doc),
                             "{\"format\":\"remember-brains\",\"version\":1,\"brains\":[{\"path\":"
                             "\"%s\",\"brain_id\":\"%s\",\"added_at\":\"" NOW "\"}]}",
                             file, id) < (int)sizeof(doc));
        given_text(scn.list, doc);
    }
    given_brain_opened(scn.other);
    copy_file_to(scn.other, scn.brain);
    r = run_args(scn.brain, "tags", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(fifo);
    free(file);
    free(id);
    free(real);
    scene_release(&scn);
}

TEST(brain_list_masks_control_bytes_in_every_field_of_a_row)
{
    Scene scn = given_scene();
    char *real = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    real = real_path_of(scn.brain);
    given_json(scn.list, "[{\"path\":\"/tmp/x\\u001b[2J\\ny\\tz\",\"brain_id\":\"i\\u001b[31m\\t\","
                         "\"added_at\":\"t\\u009b\\u007f\\t\"}]");
    r = run_args(scn.brain, "brain", "list", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "/tmp/x?[2J?y?z\ti?[31m?\tt???\n");
    cmd_result_free(&r);
    free(real);
    scene_release(&scn);
}

TEST(brain_list_and_prune_take_no_arguments)
{
    Scene scn = given_scene();
    CmdResult r = run_args(scn.brain, "brain", "list", "extra");

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "brain list takes no arguments");
    cmd_result_free(&r);
    r = run_args(scn.brain, "brain", "prune", "extra");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "brain prune takes no arguments");
    cmd_result_free(&r);
    scene_release(&scn);
}

/* ---- more ways brains.json and its folder can be wrong ---------------------- */

TEST(brain_list_a_path_that_is_not_utf8_is_not_recorded_and_the_file_stays_readable)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    BrainList list = {0};

    given_one_row(file, "/tmp/a.db", ID_A);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/\xff.db", ID_B, &probe, minted), BRAIN_LIST_IO);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    brain_list_free(&list);
    free(file);
    free(dir);
}

TEST(brain_list_a_fifo_at_the_lock_is_not_a_lock_and_does_not_block)
{
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    char *lock = given_path(dir, "brains.json.lock");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];

    ASSERT_EQ_INT(mkfifo(lock, DB_FILE_PERMS), 0);
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_IO);
    ASSERT_TRUE(access(file, F_OK) != 0);
    free(lock);
    free(file);
    free(dir);
}

TEST(brain_list_a_file_where_the_data_folder_should_be_stops_the_record)
{
    char *dir = given_dir();
    char *blocker = given_path(dir, "data");
    char *file = given_path(dir, "data/remember/brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];

    given_text(blocker, "a file");
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_IO);
    free(file);
    free(blocker);
    free(dir);
}

TEST(brain_list_a_hostile_path_in_the_list_does_not_reach_the_probe_as_a_brain)
{
    /* The module hands the probe whatever path the file holds; the probe, not the
       module, decides what is a brain. A probe that says no makes it a move. */
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    Probe never = {.still = 0};
    BrainProbe probe = given_probe(&never);
    char minted[ID_CAP];
    BrainList list = {0};

    given_json(file, "[{\"path\":\"relative/../x\",\"brain_id\":\"" ID_A "\",\"added_at\":\"t\"}]");
    ASSERT_EQ_STATUS(record_at(file, "/tmp/a.db", ID_A, &probe, minted), BRAIN_LIST_MOVED);
    ASSERT_EQ_INT(never.stills, 1);
    ASSERT_EQ_STATUS(brain_list_read(file, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, "/tmp/a.db");
    brain_list_free(&list);
    free(file);
    free(dir);
}

/* ---- brain list and prune on a file they cannot use -------------------------- */

TEST(brain_list_on_an_unreadable_file_fails_and_leaves_it_alone)
{
    Scene scn = given_scene();
    char *text = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    given_text(scn.list, "not json at all");
    r = run_args(scn.brain, "brain", "list", NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STR_PREFIX(r.err, "remember: cannot read brains.json");
    cmd_result_free(&r);
    text = file_text(scn.list);
    ASSERT_STREQ(text, "not json at all");
    free(text);
    scene_release(&scn);
}

TEST(brain_prune_on_an_unreadable_file_fails_and_leaves_it_alone)
{
    Scene scn = given_scene();
    const char *json[] = {"--json", "brain", "prune"};
    char *text = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    given_text(scn.list, "not json at all");
    r = run_remember(scn.brain, json, 3U, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,"
                             "\"type\":\"brain.prune.result\",\"error\":{\"code\":\"cannot_open\","
                             "\"message\":\"cannot read brains.json\"");
    cmd_result_free(&r);
    text = file_text(scn.list);
    ASSERT_STREQ(text, "not json at all");
    free(text);
    scene_release(&scn);
}

TEST(an_unreadable_brains_json_does_not_change_the_open_and_is_left_alone)
{
    Scene scn = given_scene();
    char *text = NULL;
    CmdResult r;

    given_brain_opened(scn.other);
    given_text(scn.list,
               "{\"format\":\"remember-brains\",\"version\":1,\"brains\":[{\"path\":1}]}");
    r = run_args(scn.brain, "add", "hello", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    text = file_text(scn.list);
    ASSERT_STR_CONTAINS(text, "\"path\":1");
    free(text);
    scene_release(&scn);
}

TEST(brain_list_and_prune_without_a_data_dir_fail_and_name_the_folder)
{
    Scene scn = given_scene();
    EnvHold xdg = env_hold("XDG_DATA_HOME", NULL);
    EnvHold home = env_hold("HOME", NULL);
    CmdResult r = run_args(scn.brain, "brain", "list", NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_PREFIX(r.err, "remember: cannot find the data folder");
    cmd_result_free(&r);
    r = run_args(scn.brain, "brain", "prune", NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_PREFIX(r.err, "remember: cannot find the data folder");
    cmd_result_free(&r);
    env_release(&home);
    env_release(&xdg);
    scene_release(&scn);
}

#ifdef REMEMBER_TEST_VFS
TEST(brain_prune_that_cannot_replace_the_file_fails_and_keeps_the_rows)
{
    Scene scn = given_scene();
    char *before = NULL;
    char *after = NULL;
    EnvHold fail = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    given_brain_opened(scn.other);
    ASSERT_EQ_INT(unlink(scn.other), 0);
    before = file_text(scn.list);
    fail = env_hold("REMEMBER_TEST_EXPORT_FAIL", "rename");
    r = run_args(scn.brain, "brain", "prune", NULL);
    env_release(&fail);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_PREFIX(r.err, "remember: cannot update brains.json");
    cmd_result_free(&r);
    after = file_text(scn.list);
    ASSERT_STREQ(after, before);
    free(after);
    free(before);
    scene_release(&scn);
}
#endif

/* ---- output that fails, and a store that fails while a copy takes its id -------- */

static int run_in_process(const char *const *argv, int argc, FILE *out, FILE *err)
{
    return remember_run(argc, (char *const *)argv, out, err);
}

TEST(brain_list_that_cannot_write_a_row_exits_1)
{
    Scene scn = given_scene();
    const char *argv[] = {"remember", "brain", "list"};
    FailingStream out_stream;
    FailingStream err_stream;
    FILE *out = NULL;
    FILE *err = NULL;

    given_brain_opened(scn.brain);
    out = failing_stream_open(&out_stream, 0U);
    err = failing_stream_open(&err_stream, FAILING_STREAM_CAP);
    if (out == NULL || err == NULL) {
        fixture_failed("failing stream");
    }
    ASSERT_EQ_INT(run_in_process(argv, 3, out, err), 1);
    ASSERT_STR_CONTAINS(err_stream.buf, "failed to write output");
    (void)fclose(out);
    (void)fclose(err);
    scene_release(&scn);
}

TEST(brain_list_that_cannot_write_the_missing_warning_exits_1)
{
    Scene scn = given_scene();
    const char *argv[] = {"remember", "brain", "list"};
    FailingStream out_stream;
    FailingStream err_stream;
    FILE *out = NULL;
    FILE *err = NULL;

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(unlink(scn.brain), 0);
    out = failing_stream_open(&out_stream, FAILING_STREAM_CAP);
    err = failing_stream_open(&err_stream, 0U);
    if (out == NULL || err == NULL) {
        fixture_failed("failing stream");
    }
    ASSERT_EQ_INT(run_in_process(argv, 3, out, err), 1);
    (void)fclose(out);
    (void)fclose(err);
    scene_release(&scn);
}

TEST(brain_list_and_prune_take_a_listed_fifo_or_folder_for_a_path_with_no_brain)
{
    Scene scn = given_scene();
    char *fifo = given_path(scn.dir, "fifo.db");
    char *folder = given_path(scn.dir, "folder.db");
    char doc[PATH_CAP * 3];
    BrainList list = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(mkfifo(fifo, DB_FILE_PERMS), 0);
    ASSERT_EQ_INT(mkdir(folder, 0700), 0);
    ASSERT_TRUE(snprintf(doc, sizeof(doc),
                         "[{\"path\":\"%s\",\"brain_id\":\"i\",\"added_at\":\"t\"},"
                         "{\"path\":\"%s\",\"brain_id\":\"j\",\"added_at\":\"t\"}]",
                         fifo, folder) < (int)sizeof(doc));
    given_json(scn.list, doc);
    r = run_args(scn.brain, "brain", "list", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "is in the list but isn't on disk.");
    cmd_result_free(&r);
    r = run_args(scn.brain, "brain", "prune", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 0);
    free(folder);
    free(fifo);
    scene_release(&scn);
}

/* Each way stat proves nothing is there: ENOENT, ENOTDIR (under a file), ELOOP (a symlink
   loop) and ENAMETOOLONG (a component past NAME_MAX). */
TEST(brain_list_and_prune_take_each_kind_of_absent_path_for_a_path_with_no_brain)
{
    Scene scn = given_scene();
    char *loop = given_path(scn.dir, "loop.db");
    char long_name[LONG_NAME];
    char doc[TEXT_CAP];
    BrainList list = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(symlink(loop, loop), 0);
    memset(long_name, 'n', sizeof(long_name) - 1U);
    long_name[sizeof(long_name) - 1U] = '\0';
    ASSERT_TRUE(snprintf(doc, sizeof(doc),
                         "[{\"path\":\"%s/gone.db\",\"brain_id\":\"i\",\"added_at\":\"t\"},"
                         "{\"path\":\"%s/under.db\",\"brain_id\":\"j\",\"added_at\":\"t\"},"
                         "{\"path\":\"%s\",\"brain_id\":\"k\",\"added_at\":\"t\"},"
                         "{\"path\":\"%s/%s\",\"brain_id\":\"l\",\"added_at\":\"t\"}]",
                         scn.dir, scn.brain, loop, scn.dir, long_name) < (int)sizeof(doc));
    given_json(scn.list, doc);
    r = run_args(scn.brain, "brain", "prune", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 0);
    free(loop);
    scene_release(&scn);
}

/* stat failing with EACCES says nothing about the brain. The row stays, and no warning says the
   brain is not on disk. */
TEST(brain_list_and_prune_keep_a_brain_whose_folder_cannot_be_searched)
{
    Scene scn = given_scene();
    char *locked = given_path(scn.dir, "locked");
    char *brain = given_path(scn.dir, "locked/one.db");
    BrainList list = {0};
    CmdResult r;

    if (geteuid() == 0) {
        (void)printf("SKIP %s (running as root; folder mode bits are ignored)\n",
                     "brain_list_and_prune_keep_a_brain_whose_folder_cannot_be_searched");
        scene_release(&scn);
        free(brain);
        free(locked);
        return;
    }
    ASSERT_EQ_INT(mkdir(locked, 0700), 0);
    given_brain_opened(brain);
    ASSERT_EQ_INT(chmod(locked, 0), 0);
    r = run_args(scn.other, "brain", "list", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    r = run_args(scn.other, "brain", "prune", NULL);
    ASSERT_EQ_INT(chmod(locked, 0700), 0);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    brain_list_free(&list);
    free(brain);
    free(locked);
    scene_release(&scn);
}

/* The brain_id the list holds for path (heap); "" when the path is not listed. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) two names of one kind, read in order
static char *listed_id_of(const char *list_file, const char *path)
{
    BrainList rows = {0};
    char *id = strdup("");
    size_t r = 0;

    ASSERT_TRUE(id != NULL);
    ASSERT_EQ_STATUS(brain_list_read(list_file, &rows), BRAIN_LIST_UNCHANGED);
    for (r = 0; r < rows.count; r++) {
        if (strcmp(rows.rows[r].path, path) == 0) {
            free(id);
            id = strdup(rows.rows[r].brain_id);
            ASSERT_TRUE(id != NULL);
        }
    }
    brain_list_free(&rows);
    return id;
}

/* One open of a copy under one fault. The copy's id and its row in the list agree, or the open
   failed; a failed open leaves the old id or the new one; the warning is printed only for an id
   that is in the brain. */
static int open_a_copy_under_fault(const Scene *scn, void (*arm)(int), int n, const char *old_id)
{
    const char *argv[] = {"remember", "--db", scn->other, "tags"};
    char *obuf = NULL;
    char *ebuf = NULL;
    size_t olen = 0;
    size_t elen = 0;
    FILE *out = open_memstream(&obuf, &olen);
    FILE *err = open_memstream(&ebuf, &elen);
    char *real = NULL;
    char *kept = NULL;
    char *listed = NULL;
    int rc = 0;

    if (out == NULL || err == NULL) {
        fixture_failed("memstream");
    }
    arm(n);
    rc = run_in_process(argv, 4, out, err);
    arm(-1);
    (void)fclose(out);
    (void)fclose(err);
    real = real_path_of(scn->other);
    kept = brain_id_in(scn->other);
    listed = listed_id_of(scn->list, real);
    ASSERT_TRUE(rc != 0 || strcmp(listed, kept) == 0);
    ASSERT_TRUE(rc == 0 || strcmp(kept, old_id) == 0 || strcmp(kept, listed) == 0);
    ASSERT_TRUE(strstr(ebuf, "looks like a copy") == NULL || strcmp(kept, old_id) != 0);
    free(listed);
    free(kept);
    free(real);
    free(ebuf);
    free(obuf);
    return rc;
}

TEST(a_copy_whose_new_id_cannot_be_stored_fails_the_open_and_leaves_brain_and_list_agreeing)
{
    void (*const arm[])(int) = {store_test_fail_exec_after, store_test_fail_prepare_after,
                                store_test_fail_step_after, store_test_fail_alloc_after};
    Scene scn = given_scene();
    char *old_id = NULL;
    char *before_list = NULL;
    int saw_error = 0;
    size_t kind = 0;

    given_brain_opened(scn.brain);
    old_id = brain_id_in(scn.brain);
    before_list = file_text(scn.list);
    for (kind = 0; kind < sizeof(arm) / sizeof(arm[0]); kind++) {
        int n = 0;

        for (n = 0; n < FAULTS_PER_KIND; n++) {
            (void)unlink(scn.other);
            copy_file_to(scn.brain, scn.other);
            given_text(scn.list, before_list);
            saw_error = (open_a_copy_under_fault(&scn, arm[kind], n, old_id) != 0) ? 1 : saw_error;
        }
    }
    ASSERT_TRUE(saw_error != 0);
    free(before_list);
    free(old_id);
    scene_release(&scn);
}

/* ---- the store side of the list ---------------------------------------------- */

static Store *given_open_store(const char *db)
{
    StoreOpenOpts opts = {.busy_timeout_ms = STORE_DEFAULT_BUSY_TIMEOUT_MS};
    char err[ERR_BUFSIZE];
    StoreError detail;
    Store *s = store_open_opts(db, &opts, err, sizeof(err), &detail);

    ASSERT_TRUE(s != NULL);
    return s;
}

/* A connection that holds an exclusive lock on db: no other connection can read it. */
static sqlite3 *given_locked(const char *db)
{
    sqlite3 *lock = NULL;

    ASSERT_EQ_INT(sqlite3_open_v2(db, &lock, SQLITE_OPEN_READWRITE, NULL), SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_exec(lock, "BEGIN EXCLUSIVE;", NULL, NULL, NULL), SQLITE_OK);
    return lock;
}

static void lock_release(sqlite3 *lock)
{
    ASSERT_EQ_INT(sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_close(lock), SQLITE_OK);
}

/* decision:2026-11-16-plan26-review-grill G2. The original is listed with the copy's id and
   stays locked past the busy timeout: the open cannot tell a copy from a move, so it fails busy
   and changes nothing. Before the review it counted as a move, and two brains kept one id. */
TEST(
    a_copy_opened_while_the_original_is_locked_past_the_busy_timeout_fails_busy_and_changes_nothing)
{
    Scene scn = given_scene();
    const char *args[] = {"--json", "--busy-timeout", "50", "tags"};
    char *real = NULL;
    char *old_id = NULL;
    char *copy_id = NULL;
    sqlite3 *lock = NULL;
    BrainList list = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    old_id = brain_id_in(scn.brain);
    real = real_path_of(scn.brain);
    copy_file_to(scn.brain, scn.other);
    lock = given_locked(scn.brain);
    r = run_remember(scn.other, args, 4U, NULL);
    lock_release(lock);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"busy\"");
    cmd_result_free(&r);
    copy_id = brain_id_in(scn.other);
    ASSERT_STREQ(copy_id, old_id);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, real);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, old_id);
    brain_list_free(&list);
    free(copy_id);
    free(old_id);
    free(real);
    scene_release(&scn);
}

/* G2: an original whose folder cannot be searched (EACCES) fails the open with cannot_open, the
   existing code of that cause, and changes nothing. */
TEST(a_copy_whose_original_s_folder_cannot_be_searched_fails_cannot_open_and_changes_nothing)
{
    Scene scn = given_scene();
    char *locked = given_path(scn.dir, "locked");
    char *inside = given_path(scn.dir, "locked/one.db");
    const char *args[] = {"--json", "tags"};
    char *real = NULL;
    char *old_id = NULL;
    char *copy_id = NULL;
    BrainList list = {0};
    CmdResult r;
    CmdResult plain;

    if (geteuid() == 0) {
        (void)printf("SKIP %s (running as root; mode bits are ignored)\n",
                     "a_copy_whose_original_s_folder_cannot_be_searched_fails_cannot_open_and_"
                     "changes_nothing");
        free(inside);
        free(locked);
        scene_release(&scn);
        return;
    }
    ASSERT_EQ_INT(mkdir(locked, 0700), 0);
    given_brain_opened(inside);
    old_id = brain_id_in(inside);
    real = real_path_of(inside);
    copy_file_to(inside, scn.other);
    ASSERT_EQ_INT(chmod(locked, 0), 0);
    r = run_remember(scn.other, args, 2U, NULL);
    plain = run_remember(scn.other, args + 1, 1U, NULL);
    ASSERT_EQ_INT(chmod(locked, 0700), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"cannot_open\"");
    /* Plain output has no envelope to collide with: only the open's failure can exit 1. */
    ASSERT_EQ_INT(plain.exit_code, 1);
    ASSERT_STREQ(plain.out, "");
    cmd_result_free(&plain);
    cmd_result_free(&r);
    copy_id = brain_id_in(scn.other);
    ASSERT_STREQ(copy_id, old_id);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, real);
    brain_list_free(&list);
    free(copy_id);
    free(old_id);
    free(real);
    free(inside);
    free(locked);
    scene_release(&scn);
}

/* Another process holds the brains.json lock for hold_ms, then lets it go. Returns once it
   holds the lock. */
static pid_t given_list_lock_held_for(const char *lock, long hold_ms)
{
    int ready[2] = {-1, -1};
    char byte = 0;
    pid_t holder = 0;

    // NOLINTNEXTLINE(android-cloexec-pipe) the child writes one end; both are closed after
    ASSERT_EQ_INT(pipe(ready), 0);
    holder = fork();
    if (holder == 0) {
        struct flock whole = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
        int fd = open(lock, O_RDWR | O_CLOEXEC);
        int missed = (fd < 0 || fcntl(fd, F_SETLK, &whole) != 0);

        (void)write(ready[1], missed != 0 ? "n" : "y", 1U);
        pause_ms(hold_ms);
        _exit(missed);
    }
    ASSERT_TRUE(holder > 0);
    ASSERT_EQ_INT(read(ready[0], &byte, 1U), 1);
    ASSERT_EQ_INT(byte, 'y');
    (void)close(ready[0]);
    (void)close(ready[1]);
    return holder;
}

static void list_lock_holder_done(pid_t holder)
{
    int status = 0;

    ASSERT_EQ_INT(waitpid(holder, &status, 0), (long long)holder);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* G5: the brains.json lock wait is bounded by the open's busy timeout. Past it the open fails
   busy and the list is left as it was. Before the review the wait had no bound. */
TEST(an_open_that_waits_past_the_busy_timeout_for_the_brains_json_lock_fails_busy)
{
    Scene scn = given_scene();
    char *lock = given_path(scn.dir, "xdg/remember/brains.json.lock");
    const char *args[] = {"--json", "--busy-timeout", "50", "tags"};
    char *before = NULL;
    char *after = NULL;
    pid_t holder = 0;
    CmdResult r;

    given_brain_opened(scn.brain);
    before = file_text(scn.list);
    holder = given_list_lock_held_for(lock, LIST_LOCK_HOLD_MS);
    r = run_remember(scn.other, args, 4U, NULL);
    list_lock_holder_done(holder);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"busy\"");
    cmd_result_free(&r);
    /* Plain output has no envelope to collide with: only the open's failure can exit 1. */
    holder = given_list_lock_held_for(lock, LIST_LOCK_HOLD_MS);
    r = run_remember(scn.other, args + 1, 3U, NULL);
    list_lock_holder_done(holder);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    after = file_text(scn.list);
    ASSERT_STREQ(after, before);
    free(after);
    free(before);
    free(lock);
    scene_release(&scn);
}

TEST(an_open_whose_brains_json_lock_is_let_go_inside_the_busy_timeout_records_the_brain)
{
    Scene scn = given_scene();
    char *lock = given_path(scn.dir, "xdg/remember/brains.json.lock");
    const char *args[] = {"--busy-timeout", "5000", "tags"};
    BrainList list = {0};
    pid_t holder = 0;
    CmdResult r;

    given_brain_opened(scn.brain);
    holder = given_list_lock_held_for(lock, LOCK_HOLD_MS);
    r = run_remember(scn.other, args, 3U, NULL);
    list_lock_holder_done(holder);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 2);
    brain_list_free(&list);
    free(lock);
    scene_release(&scn);
}

TEST(brain_prune_that_waits_past_the_busy_timeout_for_the_lock_fails_busy_and_keeps_the_rows)
{
    Scene scn = given_scene();
    char *lock = given_path(scn.dir, "xdg/remember/brains.json.lock");
    const char *args[] = {"--json", "--busy-timeout", "50", "brain", "prune"};
    char *before = NULL;
    char *after = NULL;
    pid_t holder = 0;
    CmdResult r;

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(unlink(scn.brain), 0);
    before = file_text(scn.list);
    holder = given_list_lock_held_for(lock, LIST_LOCK_HOLD_MS);
    r = run_remember(scn.brain, args, sizeof(args) / sizeof(args[0]), NULL);
    list_lock_holder_done(holder);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"busy\"");
    cmd_result_free(&r);
    after = file_text(scn.list);
    ASSERT_STREQ(after, before);
    free(after);
    free(before);
    free(lock);
    scene_release(&scn);
}

/* G5 for new-id: the brains.json lock wait is bounded and brain_note_new_id fails busy. The
   brain already holds its new id then, so the next open writes it to the row. Called on an open
   store: the command's own open would fail on the held lock before new-id ran. */
TEST(brain_new_id_that_waits_past_the_busy_timeout_for_the_lock_fails_busy_and_the_next_open_heals)
{
    Scene scn = given_scene();
    char *lock = given_path(scn.dir, "xdg/remember/brains.json.lock");
    StoreOpenOpts opts = {.busy_timeout_ms = SHORT_BUSY_MS};
    StoreBrainId ids = {0};
    StoreError why;
    char err[ERR_BUFSIZE];
    BrainList list = {0};
    char *before = NULL;
    char *after = NULL;
    char *id = NULL;
    Store *s = NULL;
    pid_t holder = 0;
    CmdResult r;

    given_brain_opened(scn.brain);
    before = file_text(scn.list);
    s = store_open_opts(scn.brain, &opts, err, sizeof(err), &why);
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ_STATUS(store_brain_new_id(s, &ids), STORE_OK);
    holder = given_list_lock_held_for(lock, LIST_LOCK_HOLD_MS);
    ASSERT_EQ_INT(brain_note_open(s), -1);
    list_lock_holder_done(holder);
    after = file_text(scn.list);
    ASSERT_STREQ(after, before);
    ASSERT_EQ_INT(brain_note_open(s), 0);
    store_close(s);
    id = brain_id_in(scn.brain);
    ASSERT_STREQ(id, ids.id);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, id);
    brain_list_free(&list);
    r = run_args(scn.brain, "tags", NULL, NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(id);
    free(after);
    free(before);
    free(lock);
    scene_release(&scn);
}

/* Plan 23: an empty brain takes the brain_id of an import file unless brains.json lists an
   existing brain with that id. A brain exported from a.db and imported into d.db would else be
   a copy of a.db that the next open renames and warns about. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, then the file it is written to
static void given_export_of(const char *db, const char *out_path)
{
    const char *args[] = {"export", "--format", "json", "--out", out_path};
    CmdResult r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, the file, then the timeout in ms
static CmdResult import_json_into(const char *db, const char *file, const char *timeout_ms)
{
    const char *args[] = {"--busy-timeout", timeout_ms, "import", "--from-json", file};

    return run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
}

TEST(an_import_of_the_export_of_a_listed_brain_keeps_the_new_brain_s_own_id_and_warns_nothing)
{
    Scene scn = given_scene();
    char *file = given_path(scn.dir, "e.json");
    char *src_id = NULL;
    char *dst_id = NULL;
    BrainList list = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    src_id = brain_id_in(scn.brain);
    given_export_of(scn.brain, file);
    r = import_json_into(scn.other, file, "5000");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    dst_id = brain_id_in(scn.other);
    ASSERT_TRUE(strcmp(dst_id, src_id) != 0);
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    r = run_args(scn.brain, "tags", NULL, NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 2);
    brain_list_free(&list);
    free(dst_id);
    free(src_id);
    free(file);
    scene_release(&scn);
}

/* The listed brain is gone (a restore): the file's id is taken, and the next open sees a move. */
TEST(an_import_of_the_export_of_a_listed_brain_that_is_gone_takes_its_id_and_the_list_follows)
{
    Scene scn = given_scene();
    char *file = given_path(scn.dir, "e.json");
    char *src_id = NULL;
    char *dst_id = NULL;
    char *real = NULL;
    BrainList list = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    src_id = brain_id_in(scn.brain);
    given_export_of(scn.brain, file);
    ASSERT_EQ_INT(unlink(scn.brain), 0);
    r = import_json_into(scn.other, file, "5000");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    dst_id = brain_id_in(scn.other);
    ASSERT_STREQ(dst_id, src_id);
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    real = real_path_of(scn.other);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, real);
    ASSERT_STREQ(row_at(&list, 0)->brain_id, src_id);
    brain_list_free(&list);
    free(real);
    free(dst_id);
    free(src_id);
    free(file);
    scene_release(&scn);
}

/* A hand-edited list can name one id twice. The first row that still has it decides; a later
   row whose brain is gone does not undo that. */
TEST(an_import_whose_id_is_listed_twice_with_the_first_brain_live_keeps_the_new_brain_s_own_id)
{
    Scene scn = given_scene();
    char *file = given_path(scn.dir, "e.json");
    char *gone = given_path(scn.dir, "gone.db");
    char doc[PATH_CAP * 3];
    char *real = NULL;
    char *src_id = NULL;
    char *dst_id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    real = real_path_of(scn.brain);
    src_id = brain_id_in(scn.brain);
    given_export_of(scn.brain, file);
    ASSERT_TRUE(snprintf(doc, sizeof(doc),
                         "[{\"path\":\"%s\",\"brain_id\":\"%s\",\"added_at\":\"t\"},"
                         "{\"path\":\"%s\",\"brain_id\":\"%s\",\"added_at\":\"t\"}]",
                         real, src_id, gone, src_id) < (int)sizeof(doc));
    given_json(scn.list, doc);
    r = import_json_into(scn.other, file, "5000");
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    dst_id = brain_id_in(scn.other);
    ASSERT_TRUE(strcmp(dst_id, src_id) != 0);
    free(dst_id);
    free(src_id);
    free(real);
    free(gone);
    free(file);
    scene_release(&scn);
}

/* The restore took the id, so the import records it as new-id does: a copy made before the
   restored brain is opened again is the one flagged, not the restored brain. */
TEST(an_import_that_takes_the_file_s_id_records_it_so_a_copy_opened_first_is_the_one_flagged)
{
    Scene scn = given_scene();
    char *file = given_path(scn.dir, "e.json");
    char *copy = given_path(scn.dir, "three.db");
    char *src_id = NULL;
    char *id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    src_id = brain_id_in(scn.brain);
    given_export_of(scn.brain, file);
    ASSERT_EQ_INT(unlink(scn.brain), 0);
    r = import_json_into(scn.other, file, "5000");
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    copy_file_to(scn.other, copy);
    r = run_args(copy, "tags", NULL, NULL);
    ASSERT_STR_CONTAINS(r.err, "looks like a copy of another brain");
    cmd_result_free(&r);
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    id = brain_id_in(scn.other);
    ASSERT_STREQ(id, src_id);
    free(id);
    free(src_id);
    free(copy);
    free(file);
    scene_release(&scn);
}

/* The listed brain cannot be checked: the import fails visibly and the new brain stays empty. */
TEST(
    an_import_whose_listed_source_brain_is_locked_past_the_busy_timeout_fails_busy_and_imports_nothing)
{
    Scene scn = given_scene();
    char *file = given_path(scn.dir, "e.json");
    char *src_id = NULL;
    char *dst_id = NULL;
    sqlite3 *lock = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    src_id = brain_id_in(scn.brain);
    given_export_of(scn.brain, file);
    lock = given_locked(scn.brain);
    r = import_json_into(scn.other, file, "50");
    lock_release(lock);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot tell whether");
    cmd_result_free(&r);
    dst_id = brain_id_in(scn.other);
    ASSERT_TRUE(strcmp(dst_id, src_id) != 0);
    r = run_args(scn.other, "list", NULL, NULL);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(dst_id);
    free(src_id);
    free(file);
    scene_release(&scn);
}

/* An unreadable brains.json lists nothing: the open goes on, and so does the import. */
TEST(an_import_with_an_unreadable_brains_json_takes_the_file_s_id)
{
    Scene scn = given_scene();
    char *file = given_path(scn.dir, "e.json");
    char *src_id = NULL;
    char *dst_id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    src_id = brain_id_in(scn.brain);
    given_export_of(scn.brain, file);
    given_text(scn.list, "not json");
    r = import_json_into(scn.other, file, "5000");
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    dst_id = brain_id_in(scn.other);
    ASSERT_STREQ(dst_id, src_id);
    free(dst_id);
    free(src_id);
    free(file);
    scene_release(&scn);
}

TEST(a_copy_that_could_not_be_told_is_a_copy_on_the_next_open)
{
    Scene scn = given_scene();
    const char *args[] = {"--busy-timeout", "50", "tags"};
    sqlite3 *lock = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    copy_file_to(scn.brain, scn.other);
    lock = given_locked(scn.brain);
    r = run_remember(scn.other, args, 3U, NULL);
    lock_release(lock);
    cmd_result_free(&r);
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "looks like a copy of another brain");
    cmd_result_free(&r);
    scene_release(&scn);
}

/* The probe waits the open's busy timeout: a lock let go inside it does not stop the check. */
TEST(a_copy_whose_original_is_locked_for_less_than_the_busy_timeout_is_a_copy)
{
    Scene scn = given_scene();
    const char *args[] = {"--busy-timeout", "5000", "tags"};
    int ready[2] = {-1, -1};
    char byte = 0;
    int status = 0;
    pid_t holder = 0;
    CmdResult r;

    given_brain_opened(scn.brain);
    copy_file_to(scn.brain, scn.other);
    // NOLINTNEXTLINE(android-cloexec-pipe) the child writes one end; both are closed after
    ASSERT_EQ_INT(pipe(ready), 0);
    holder = fork();
    if (holder == 0) {
        sqlite3 *lock = NULL;
        int missed = (sqlite3_open_v2(scn.brain, &lock, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK ||
                      sqlite3_exec(lock, "BEGIN EXCLUSIVE;", NULL, NULL, NULL) != SQLITE_OK);

        (void)write(ready[1], missed != 0 ? "n" : "y", 1U);
        pause_ms(LOCK_HOLD_MS);
        (void)sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
        (void)sqlite3_close(lock);
        _exit(missed);
    }
    ASSERT_TRUE(holder > 0);
    ASSERT_EQ_INT(read(ready[0], &byte, 1U), 1);
    ASSERT_EQ_INT(byte, 'y');
    r = run_remember(scn.other, args, 3U, NULL);
    ASSERT_EQ_INT(waitpid(holder, &status, 0), (long long)holder);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "looks like a copy of another brain");
    cmd_result_free(&r);
    (void)close(ready[0]);
    (void)close(ready[1]);
    scene_release(&scn);
}

/* decision:2026-11-16-plan26-review-grill G6 B: a field the reader does not take is warned with
   the C-35 file line, once per command. */
static const char k_unknown_doc[] =
    "[{\"path\":\"/nope/a.db\",\"brain_id\":\"i\",\"added_at\":\"t\",\"note\":1},"
    "{\"path\":\"/nope/b.db\",\"brain_id\":\"j\",\"added_at\":\"t\",\"tag\":2}]";

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the output, then the file it names
static void assert_one_c35_line(const char *err, const char *list_file, const char *field)
{
    char want[PATH_CAP * 2];
    const char *at = NULL;

    ASSERT_TRUE(snprintf(want, sizeof(want),
                         "remember: warning: \"%s\" has a field remember doesn't know, \"%s\". "
                         "The rest of the file was read. Remove that field, or update remember if "
                         "the file came from a newer version.",
                         list_file, field) < (int)sizeof(want));
    at = strstr(err, want);
    ASSERT_TRUE(at != NULL);
    if (at != NULL) {
        ASSERT_TRUE(strstr(at + strlen(want), "has a field remember doesn't know") == NULL);
    }
}

TEST(brain_list_warns_the_first_unknown_field_with_the_c35_file_line)
{
    Scene scn = given_scene();
    CmdResult r;

    given_brain_opened(scn.brain);
    given_json(scn.list, k_unknown_doc);
    r = run_args(scn.brain, "brain", "list", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    assert_one_c35_line(r.err, scn.list, "note");
    cmd_result_free(&r);
    scene_release(&scn);
}

TEST(brain_list_warns_an_unknown_root_field_with_the_c35_file_line)
{
    Scene scn = given_scene();
    CmdResult r;

    given_brain_opened(scn.brain);
    given_text(scn.list, "{\"format\":\"remember-brains\",\"version\":1,\"brains\":[],\"x\":0}");
    r = run_args(scn.brain, "brain", "list", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    assert_one_c35_line(r.err, scn.list, "x");
    cmd_result_free(&r);
    scene_release(&scn);
}

TEST(brain_prune_warns_an_unknown_field_with_the_c35_file_line)
{
    Scene scn = given_scene();
    CmdResult r;

    given_brain_opened(scn.brain);
    given_json(scn.list, k_unknown_doc);
    r = run_args(scn.brain, "brain", "prune", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    assert_one_c35_line(r.err, scn.list, "note");
    cmd_result_free(&r);
    scene_release(&scn);
}

/* The brain's own row carries the unknown member, so the open leaves the file as it is and
   new-id reads it again: diag warns each file once. */
TEST(brain_new_id_warns_an_unknown_field_in_brains_json_once)
{
    Scene scn = given_scene();
    char doc[PATH_CAP * 2];
    char *real = NULL;
    char *id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    real = real_path_of(scn.brain);
    id = brain_id_in(scn.brain);
    ASSERT_TRUE(snprintf(doc, sizeof(doc),
                         "[{\"path\":\"%s\",\"brain_id\":\"%s\",\"added_at\":\"t\",\"note\":1}]",
                         real, id) < (int)sizeof(doc));
    given_json(scn.list, doc);
    r = run_args(scn.brain, "brain", "new-id", NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    assert_one_c35_line(r.err, scn.list, "note");
    cmd_result_free(&r);
    free(id);
    free(real);
    scene_release(&scn);
}

TEST(a_read_write_open_warns_an_unknown_field_in_brains_json)
{
    Scene scn = given_scene();
    CmdResult r;

    given_brain_opened(scn.brain);
    given_json(scn.list, k_unknown_doc);
    r = run_args(scn.brain, "tags", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    assert_one_c35_line(r.err, scn.list, "note");
    cmd_result_free(&r);
    scene_release(&scn);
}

TEST(brain_list_json_carries_unknown_field_with_field_and_where)
{
    Scene scn = given_scene();
    const char *json[] = {"--json", "brain", "list"};
    char want[PATH_CAP * 2];
    CmdResult r;

    given_brain_opened(scn.brain);
    given_json(scn.list, k_unknown_doc);
    r = run_remember(scn.brain, json, 3U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(
        snprintf(want, sizeof(want),
                 "\"code\":\"unknown_field\",\"message\":\"\\\"%s\\\" has a field remember "
                 "doesn't know, \\\"note\\\". The rest of the file was read.\",\"fix\":\"Remove "
                 "that field, or update remember if the file came from a newer version.\"",
                 scn.list) < (int)sizeof(want));
    ASSERT_STR_CONTAINS(r.out, want);
    ASSERT_STR_CONTAINS(r.out, "\"detail\":{\"field\":\"note\"");
    cmd_result_free(&r);
    scene_release(&scn);
}

/* The unknown member's name is copied out of the parsed document: an allocation that fails there
   is BRAIN_LIST_IO with nothing handed back, never a list without its warning. */
TEST(brain_list_read_of_a_file_with_an_unknown_member_survives_each_allocation_failure)
{
    enum { SWEEP = 30 };
    char *dir = given_dir();
    char *file = given_path(dir, "brains.json");
    int failed = 0;
    int n = 0;

    given_json(file, k_unknown_doc);
    for (n = 0; n < SWEEP; n++) {
        BrainList list = {0};
        BrainListCode code = BRAIN_LIST_IO;

        brain_list_test_fail_alloc_after(n);
        code = brain_list_read(file, &list);
        brain_list_test_fail_alloc_after(-1);
        if (code == BRAIN_LIST_UNCHANGED) {
            ASSERT_STREQ(list.unknown_field, "note");
            ASSERT_EQ_INT((long long)list.count, 2);
        } else {
            ASSERT_EQ_STATUS(code, BRAIN_LIST_IO);
            ASSERT_TRUE(list.rows == NULL && list.unknown_field == NULL);
            failed = 1;
        }
        brain_list_free(&list);
    }
    ASSERT_EQ_INT(failed, 1);
    free(file);
    free(dir);
}

/* argv runs in process with stdout failing after out_cap bytes and stderr after err_cap. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) stdout's cap, then stderr's, as in argv
static int run_with_failing_streams(const char *const *argv, int argc, size_t out_cap,
                                    size_t err_cap)
{
    FailingStream out_stream;
    FailingStream err_stream;
    FILE *out = failing_stream_open(&out_stream, out_cap);
    FILE *err = failing_stream_open(&err_stream, err_cap);
    int rc = 0;

    if (out == NULL || err == NULL) {
        fixture_failed("failing stream");
    }
    rc = run_in_process(argv, argc, out, err);
    (void)fclose(out);
    (void)fclose(err);
    return rc;
}

TEST(brain_list_and_prune_that_cannot_write_the_unknown_field_warning_exit_1)
{
    Scene scn = given_scene();
    const char *list_argv[] = {"remember", "brain", "list"};
    const char *prune_argv[] = {"remember", "brain", "prune"};

    given_brain_opened(scn.brain);
    given_json(scn.list, k_unknown_doc);
    ASSERT_EQ_INT(run_with_failing_streams(list_argv, 3, FAILING_STREAM_CAP, 0U), 1);
    ASSERT_EQ_INT(run_with_failing_streams(prune_argv, 3, FAILING_STREAM_CAP, 0U), 1);
    scene_release(&scn);
}

TEST(brain_list_and_prune_json_that_cannot_write_the_data_exit_1)
{
    Scene scn = given_scene();
    const char *list_argv[] = {"remember", "--json", "brain", "list"};
    const char *prune_argv[] = {"remember", "--json", "brain", "prune"};

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(run_with_failing_streams(list_argv, 4, 0U, FAILING_STREAM_CAP), 1);
    ASSERT_EQ_INT(unlink(scn.brain), 0);
    ASSERT_EQ_INT(run_with_failing_streams(prune_argv, 4, 0U, FAILING_STREAM_CAP), 1);
    scene_release(&scn);
}

TEST(store_path_has_brain_id_cannot_tell_for_a_brain_under_an_exclusive_lock)
{
    Scene scn = given_scene();
    StoreError why = {.error_class = STORE_ERROR_NONE};
    char *id = NULL;
    sqlite3 *lock = NULL;

    given_brain_opened(scn.brain);
    id = brain_id_in(scn.brain);
    lock = given_locked(scn.brain);
    ASSERT_EQ_INT(store_path_has_brain_id(scn.brain, id, 0, &why), -1);
    lock_release(lock);
    ASSERT_EQ_INT(why.error_class, STORE_ERROR_BUSY);
    free(id);
    scene_release(&scn);
}

TEST(store_path_has_brain_id_cannot_tell_for_a_path_it_may_not_see_or_open)
{
    Scene scn = given_scene();
    StoreError why = {.error_class = STORE_ERROR_NONE};
    char *locked = given_path(scn.dir, "locked");
    char *inside = given_path(scn.dir, "locked/one.db");
    char *id = NULL;

    if (geteuid() == 0) {
        (void)printf("SKIP %s (running as root; mode bits are ignored)\n",
                     "store_path_has_brain_id_cannot_tell_for_a_path_it_may_not_see_or_open");
        free(inside);
        free(locked);
        scene_release(&scn);
        return;
    }
    given_brain_opened(scn.brain);
    id = brain_id_in(scn.brain);
    ASSERT_EQ_INT(chmod(scn.brain, 0), 0);
    ASSERT_EQ_INT(store_path_has_brain_id(scn.brain, id, 0, &why), -1);
    ASSERT_EQ_INT(chmod(scn.brain, DB_FILE_PERMS), 0);
    ASSERT_EQ_INT(why.error_class, STORE_ERROR_CANNOT_OPEN);
    ASSERT_EQ_INT(mkdir(locked, 0700), 0);
    given_brain_opened(inside);
    ASSERT_EQ_INT(chmod(locked, 0), 0);
    ASSERT_EQ_INT(store_path_has_brain_id(inside, id, 0, &why), -1);
    ASSERT_EQ_INT(chmod(locked, 0700), 0);
    ASSERT_EQ_INT(why.error_class, STORE_ERROR_CANNOT_OPEN);
    free(id);
    free(inside);
    free(locked);
    scene_release(&scn);
}

TEST(store_path_has_brain_id_is_0_for_an_empty_file_and_a_brain_meta_with_no_row)
{
    Scene scn = given_scene();
    sqlite3 *db = NULL;

    given_text(scn.other, "");
    ASSERT_EQ_INT(store_path_has_brain_id(scn.other, ID_A, 0, NULL), 0);
    ASSERT_EQ_INT(sqlite3_open_v2(scn.brain, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL),
                  SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_exec(db, "CREATE TABLE brain_meta(brain_id TEXT);", NULL, NULL, NULL),
                  SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_close(db), SQLITE_OK);
    ASSERT_EQ_INT(store_path_has_brain_id(scn.brain, ID_A, 0, NULL), 0);
    scene_release(&scn);
}

TEST(store_busy_timeout_ms_reads_the_open_s_timeout)
{
    Scene scn = given_scene();
    StoreOpenOpts opts = {.busy_timeout_ms = ODD_BUSY_MS};
    char err[ERR_BUFSIZE];
    StoreError detail;
    Store *s = NULL;

    given_brain_opened(scn.brain);
    s = store_open_opts(scn.brain, &opts, err, sizeof(err), &detail);
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ_INT(store_busy_timeout_ms(s), 1234);
    ASSERT_EQ_INT(store_busy_timeout_ms(NULL), 0);
    store_close(s);
    scene_release(&scn);
}

TEST(store_path_has_brain_id_is_1_for_the_id_a_brain_holds_0_for_another_and_minus_1_without_input)
{
    Scene scn = given_scene();
    char *id = NULL;
    char *link = given_path(scn.dir, "link.db");

    given_brain_opened(scn.brain);
    id = brain_id_in(scn.brain);
    ASSERT_EQ_INT(store_path_has_brain_id(scn.brain, id, 0, NULL), 1);
    ASSERT_EQ_INT(store_path_has_brain_id(scn.brain, ID_A, 0, NULL), 0);
    ASSERT_EQ_INT(symlink(scn.brain, link), 0);
    ASSERT_EQ_INT(store_path_has_brain_id(link, id, 0, NULL), 1);
    ASSERT_EQ_INT(store_path_has_brain_id(scn.other, id, 0, NULL), 0);
    ASSERT_EQ_INT(store_path_has_brain_id(NULL, id, 0, NULL), -1);
    ASSERT_EQ_INT(store_path_has_brain_id(scn.brain, NULL, 0, NULL), -1);
    free(link);
    free(id);
    scene_release(&scn);
}

TEST(store_path_has_brain_id_is_0_for_a_file_that_is_not_a_brain_or_not_a_file)
{
    Scene scn = given_scene();
    char *fifo = given_path(scn.dir, "fifo.db");
    char *folder = given_path(scn.dir, "folder.db");

    given_text(scn.other, "this is not a database file at all, only text\n");
    ASSERT_EQ_INT(store_path_has_brain_id(scn.other, ID_A, 0, NULL), 0);
    ASSERT_EQ_INT(mkfifo(fifo, DB_FILE_PERMS), 0);
    ASSERT_EQ_INT(store_path_has_brain_id(fifo, ID_A, 0, NULL), 0);
    ASSERT_EQ_INT(mkdir(folder, 0700), 0);
    ASSERT_EQ_INT(store_path_has_brain_id(folder, ID_A, 0, NULL), 0);
    free(folder);
    free(fifo);
    scene_release(&scn);
}

TEST(store_brain_set_id_replaces_the_id_and_store_brain_id_reads_it)
{
    Scene scn = given_scene();
    Store *s = NULL;
    char got[ID_CAP];
    char *saved = NULL;

    given_brain_opened(scn.brain);
    s = given_open_store(scn.brain);
    saved = brain_id_in(scn.brain);
    ASSERT_EQ_STATUS(store_brain_set_id(s, ID_MINTED), STORE_OK);
    ASSERT_EQ_STATUS(store_brain_id(s, got, sizeof(got)), STORE_OK);
    ASSERT_STREQ(got, ID_MINTED);
    ASSERT_EQ_STATUS(store_brain_set_id(s, "not-an-id"), STORE_ERR_INTERNAL);
    ASSERT_EQ_STATUS(store_brain_set_id(NULL, ID_MINTED), STORE_ERR_INTERNAL);
    ASSERT_EQ_STATUS(store_brain_id(s, got, 8U), STORE_ERR_INTERNAL);
    ASSERT_EQ_STATUS(store_brain_id(NULL, got, sizeof(got)), STORE_ERR_INTERNAL);
    ASSERT_TRUE(strcmp(saved, ID_MINTED) != 0);
    store_close(s);
    free(saved);
    scene_release(&scn);
}

TEST(store_mint_uuid_writes_a_canonical_v7_id_and_refuses_a_short_buffer)
{
    char a[ID_CAP];
    char b[ID_CAP];
    char tiny[SMALL_BUF];

    ASSERT_EQ_INT(store_mint_uuid(a, sizeof(a)), 0);
    ASSERT_EQ_INT(store_mint_uuid(b, sizeof(b)), 0);
    ASSERT_EQ_INT((long long)strlen(a), STORE_DEVICE_ID_LEN);
    ASSERT_EQ_INT(a[14], '7');
    ASSERT_TRUE(strcmp(a, b) != 0);
    ASSERT_EQ_INT(store_mint_uuid(tiny, sizeof(tiny)), -1);
    ASSERT_EQ_INT(store_mint_uuid(NULL, sizeof(a)), -1);
}

TEST(store_is_writable_is_true_for_a_plain_open_and_false_for_no_store)
{
    Scene scn = given_scene();
    Store *s = NULL;

    given_brain_opened(scn.brain);
    s = given_open_store(scn.brain);
    ASSERT_TRUE(store_is_writable(s));
    ASSERT_FALSE(store_is_writable(NULL));
    store_close(s);
    scene_release(&scn);
}

TEST(a_newer_brain_is_not_recorded_keeps_its_id_and_is_not_taken_for_a_copy)
{
    Scene scn = given_scene();
    BrainList list = {0};
    char *old_id = NULL;
    char *kept_id = NULL;
    CmdResult r;

    given_brain_opened(scn.brain);
    old_id = brain_id_in(scn.brain);
    copy_file_to(scn.brain, scn.other);
    free(harness_sqlite_query_line(scn.other,
                                   "UPDATE brain_meta SET min_reader_version = '9.9.9';"));
    r = run_args(scn.other, "tags", NULL, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "read-only here");
    ASSERT_STR_NOT_CONTAINS(r.err, "looks like a copy");
    cmd_result_free(&r);
    kept_id = brain_id_in(scn.other);
    ASSERT_STREQ(kept_id, old_id);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    brain_list_free(&list);
    free(kept_id);
    free(old_id);
    scene_release(&scn);
}

TEST(a_brain_file_that_cannot_be_written_is_not_recorded)
{
    Scene scn = given_scene();
    BrainList list = {0};
    CmdResult r;

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(unlink(scn.list), 0);
    ASSERT_EQ_INT(chmod(scn.brain, 0400), 0);
    r = run_args(scn.brain, "tags", NULL, NULL);
    (void)chmod(scn.brain, DB_FILE_PERMS);
    cmd_result_free(&r);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 0);
    scene_release(&scn);
}

TEST(a_brain_opened_through_a_symlink_is_listed_by_its_real_path)
{
    Scene scn = given_scene();
    BrainList list = {0};
    char *real = NULL;
    char *link = given_path(scn.dir, "link.db");

    given_brain_opened(scn.brain);
    ASSERT_EQ_INT(unlink(scn.list), 0);
    ASSERT_EQ_INT(symlink(scn.brain, link), 0);
    given_brain_opened(link);
    real = real_path_of(scn.brain);
    ASSERT_EQ_STATUS(brain_list_read(scn.list, &list), BRAIN_LIST_UNCHANGED);
    ASSERT_EQ_INT((long long)list.count, 1);
    ASSERT_STREQ(row_at(&list, 0)->path, real);
    brain_list_free(&list);
    free(real);
    free(link);
    scene_release(&scn);
}

/* ---- the data dir (decision:g-01) ------------------------------------------ */

static void data_dir_is(const char *want)
{
    char got[PATH_CAP];
    char err[ID_CAP];

    ASSERT_EQ_INT(util_resolve_data_dir(got, sizeof(got), err, sizeof(err)), 0);
    ASSERT_STREQ(got, want);
}

TEST(data_dir_is_xdg_data_home_remember_when_it_is_set)
{
    EnvHold xdg = env_hold("XDG_DATA_HOME", "/tmp/xdg-x");
    EnvHold home = env_hold("HOME", "/tmp/home-x");
    EnvHold user_db = env_hold("REMEMBER_USER_DB", "/tmp/elsewhere/user.db");

    data_dir_is("/tmp/xdg-x/remember");
    env_release(&user_db);
    env_release(&home);
    env_release(&xdg);
}

TEST(data_dir_is_home_dot_remember_when_xdg_data_home_is_unset_or_empty)
{
    EnvHold xdg = env_hold("XDG_DATA_HOME", NULL);
    EnvHold home = env_hold("HOME", "/tmp/home-x");

    data_dir_is("/tmp/home-x/.remember");
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
    ASSERT_EQ_INT(setenv("XDG_DATA_HOME", "", 1), 0);
    data_dir_is("/tmp/home-x/.remember");
    env_release(&home);
    env_release(&xdg);
}

TEST(data_dir_without_xdg_data_home_or_home_is_an_error)
{
    EnvHold xdg = env_hold("XDG_DATA_HOME", NULL);
    EnvHold home = env_hold("HOME", NULL);
    char got[PATH_CAP];
    char err[ID_CAP];

    ASSERT_EQ_INT(util_resolve_data_dir(got, sizeof(got), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "HOME is not set");
    env_release(&home);
    env_release(&xdg);
}

TEST(data_dir_that_does_not_fit_the_buffer_is_an_error)
{
    EnvHold xdg = env_hold("XDG_DATA_HOME", "/tmp/xdg-x");
    char got[SMALL_BUF];
    char err[ID_CAP];

    ASSERT_EQ_INT(util_resolve_data_dir(got, sizeof(got), err, sizeof(err)), -1);
    ASSERT_EQ_INT(util_resolve_data_dir(NULL, 0U, err, sizeof(err)), -1);
    env_release(&xdg);
}

TEST(user_db_still_follows_remember_user_db_then_the_data_dir)
{
    EnvHold xdg = env_hold("XDG_DATA_HOME", "/tmp/xdg-x");
    EnvHold user_db = env_hold("REMEMBER_USER_DB", "/tmp/elsewhere/user.db");
    char got[PATH_CAP];
    char err[ID_CAP];

    ASSERT_EQ_INT(util_resolve_user_db_path(got, sizeof(got), err, sizeof(err)), 0);
    ASSERT_STREQ(got, "/tmp/elsewhere/user.db");
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test process
    ASSERT_EQ_INT(unsetenv("REMEMBER_USER_DB"), 0);
    ASSERT_EQ_INT(util_resolve_user_db_path(got, sizeof(got), err, sizeof(err)), 0);
    ASSERT_STREQ(got, "/tmp/xdg-x/remember/user.db");
    env_release(&user_db);
    env_release(&xdg);
}

void register_brain_list_tests(void)
{
    RUN_TEST(brain_list_first_record_adds_one_row_and_a_second_adds_none);
    RUN_TEST(brain_list_file_mode_is_0600);
    RUN_TEST(brain_list_write_is_a_temp_file_renamed_over_brains_json);
    RUN_TEST(brain_list_stale_temp_symlink_is_replaced_not_followed);
    RUN_TEST(brain_list_record_holds_the_advisory_lock_while_it_decides);
    RUN_TEST(brain_list_a_copy_takes_the_minted_id_and_keeps_the_original_row);
    RUN_TEST(brain_list_a_copy_whose_path_is_listed_gets_the_minted_id_in_that_row);
    RUN_TEST(brain_list_a_copy_whose_mint_fails_changes_nothing);
    RUN_TEST(brain_list_a_plain_open_mints_nothing);
    RUN_TEST(brain_list_a_move_keeps_the_id_and_changes_the_path);
    RUN_TEST(brain_list_a_move_onto_a_listed_path_drops_the_stale_row_of_that_path);
    RUN_TEST(brain_list_a_listed_path_with_a_new_id_takes_that_id);
    RUN_TEST(brain_list_a_probe_that_cannot_tell_writes_nothing_and_mints_nothing);
    RUN_TEST(brain_list_read_names_the_first_unknown_member_root_first);
    RUN_TEST(brain_list_read_names_an_unknown_member_of_a_later_row);
    RUN_TEST(brain_list_read_of_a_file_with_only_known_members_names_none);
    RUN_TEST(brain_list_record_and_prune_hand_the_unknown_member_to_the_caller);
    RUN_TEST(brain_list_read_of_a_missing_file_is_an_empty_list);
    RUN_TEST(brain_list_a_record_that_changes_nothing_leaves_the_file_as_it_is);
    RUN_TEST(brain_list_a_folder_that_cannot_take_the_temp_file_returns_io_and_keeps_the_file);
    RUN_TEST(brain_list_prune_drops_a_missing_path_and_returns_its_row);
    RUN_TEST(brain_list_prune_with_nothing_missing_does_not_rewrite_the_file);
    RUN_TEST(brain_list_prune_of_a_missing_file_creates_nothing);
    RUN_TEST(brain_list_calls_without_a_path_or_a_probe_fail_without_writing);
    RUN_TEST(brain_list_a_nul_inside_a_path_is_unreadable);
    RUN_TEST(brain_list_a_nul_inside_a_brain_id_is_unreadable);
    RUN_TEST(brain_list_a_raw_nul_byte_in_the_file_is_unreadable);
    RUN_TEST(brain_list_a_row_with_a_wrong_type_or_a_missing_field_is_unreadable);
    RUN_TEST(brain_list_a_document_of_another_shape_is_unreadable);
    RUN_TEST(brain_list_a_row_field_given_twice_is_unreadable);
    RUN_TEST(brain_list_a_document_member_given_twice_is_unreadable);
    RUN_TEST(brain_list_an_unknown_member_given_twice_is_read);
    RUN_TEST(brain_list_has_no_row_cap);
    RUN_TEST(brain_list_a_file_of_exactly_the_cap_is_read);
    RUN_TEST(brain_list_a_file_one_byte_over_the_cap_is_unreadable_and_left_alone);
    RUN_TEST(brain_list_an_unreadable_file_is_never_written_over);
    RUN_TEST(brain_list_a_fifo_at_brains_json_is_unreadable_and_does_not_block);
    RUN_TEST(brain_list_a_folder_at_brains_json_is_unreadable);
    RUN_TEST(brain_list_a_symlink_at_brains_json_to_a_regular_file_is_read);
    RUN_TEST(brain_list_a_symlink_at_the_lock_is_not_followed);
    RUN_TEST(brain_list_a_lock_that_cannot_be_made_leaves_the_previous_file);
    RUN_TEST(brain_list_add_survives_each_allocation_failure);
    RUN_TEST(brain_list_a_copy_survives_each_allocation_failure);
    RUN_TEST(brain_list_a_copy_of_a_listed_path_survives_each_allocation_failure);
    RUN_TEST(brain_list_a_move_survives_each_allocation_failure);
    RUN_TEST(brain_list_an_id_update_survives_each_allocation_failure);
    RUN_TEST(brain_list_prune_survives_each_allocation_failure);
    RUN_TEST(brain_list_read_survives_each_allocation_failure);
    RUN_TEST(brain_list_a_fifo_at_brains_json_is_not_read_even_when_it_holds_a_whole_document);
    RUN_TEST(brain_list_eight_concurrent_records_all_keep_their_row);
    RUN_TEST(brain_list_a_path_that_is_not_utf8_is_not_recorded_and_the_file_stays_readable);
    RUN_TEST(brain_list_a_fifo_at_the_lock_is_not_a_lock_and_does_not_block);
    RUN_TEST(brain_list_a_file_where_the_data_folder_should_be_stops_the_record);
    RUN_TEST(brain_list_a_hostile_path_in_the_list_does_not_reach_the_probe_as_a_brain);
    RUN_TEST(data_dir_is_xdg_data_home_remember_when_it_is_set);
    RUN_TEST(data_dir_is_home_dot_remember_when_xdg_data_home_is_unset_or_empty);
    RUN_TEST(data_dir_without_xdg_data_home_or_home_is_an_error);
    RUN_TEST(data_dir_that_does_not_fit_the_buffer_is_an_error);
    RUN_TEST(user_db_still_follows_remember_user_db_then_the_data_dir);
    RUN_TEST(opening_a_brain_read_write_adds_one_row_and_a_second_open_adds_none);
    RUN_TEST(brains_json_follows_the_data_dir_and_not_the_user_db);
    RUN_TEST(brains_json_is_in_home_dot_remember_when_xdg_data_home_is_unset);
    RUN_TEST(the_data_dir_is_made_with_mode_0700_when_it_is_missing);
    RUN_TEST(a_copied_brain_gets_a_new_id_and_prints_the_c38_line_with_its_fix);
    RUN_TEST(a_copied_brain_warns_once_and_json_carries_message_and_fix);
    RUN_TEST(a_moved_brain_keeps_its_id_updates_the_path_and_warns_nothing);
    RUN_TEST(brain_new_id_is_recorded_on_the_next_open_without_a_second_row);
    RUN_TEST(brain_new_id_updates_brains_json_so_a_copy_of_the_new_id_is_the_one_flagged);
    RUN_TEST(
        brain_new_id_that_waits_past_the_busy_timeout_for_the_lock_fails_busy_and_the_next_open_heals);
    RUN_TEST(
        brain_list_prints_path_id_and_added_at_per_row_and_a_path_with_no_brain_prints_the_c01_line);
    RUN_TEST(brain_list_json_gives_the_rows_and_a_brain_missing_warning_with_its_fix);
    RUN_TEST(brain_list_with_no_file_prints_nothing_and_creates_no_brain);
    RUN_TEST(brain_prune_drops_a_path_with_no_brain_and_keeps_one_that_has_it);
    RUN_TEST(brain_prune_json_names_the_dropped_rows);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(a_failed_replace_leaves_the_previous_file_and_the_open_exits_as_before);
#endif
    RUN_TEST(a_fifo_at_a_listed_brain_path_does_not_block_a_later_open);
    RUN_TEST(brain_list_masks_control_bytes_in_every_field_of_a_row);
    RUN_TEST(brain_list_and_prune_take_no_arguments);
    RUN_TEST(brain_list_on_an_unreadable_file_fails_and_leaves_it_alone);
    RUN_TEST(brain_prune_on_an_unreadable_file_fails_and_leaves_it_alone);
    RUN_TEST(an_unreadable_brains_json_does_not_change_the_open_and_is_left_alone);
    RUN_TEST(brain_list_and_prune_without_a_data_dir_fail_and_name_the_folder);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(brain_prune_that_cannot_replace_the_file_fails_and_keeps_the_rows);
#endif
    RUN_TEST(brain_list_that_cannot_write_a_row_exits_1);
    RUN_TEST(brain_list_that_cannot_write_the_missing_warning_exits_1);
    RUN_TEST(brain_list_and_prune_take_a_listed_fifo_or_folder_for_a_path_with_no_brain);
    RUN_TEST(brain_list_and_prune_take_each_kind_of_absent_path_for_a_path_with_no_brain);
    RUN_TEST(brain_list_and_prune_keep_a_brain_whose_folder_cannot_be_searched);
    RUN_TEST(
        a_copy_whose_new_id_cannot_be_stored_fails_the_open_and_leaves_brain_and_list_agreeing);
    RUN_TEST(
        a_copy_opened_while_the_original_is_locked_past_the_busy_timeout_fails_busy_and_changes_nothing);
    RUN_TEST(
        a_copy_whose_original_s_folder_cannot_be_searched_fails_cannot_open_and_changes_nothing);
    RUN_TEST(an_open_that_waits_past_the_busy_timeout_for_the_brains_json_lock_fails_busy);
    RUN_TEST(an_open_whose_brains_json_lock_is_let_go_inside_the_busy_timeout_records_the_brain);
    RUN_TEST(
        brain_prune_that_waits_past_the_busy_timeout_for_the_lock_fails_busy_and_keeps_the_rows);
    RUN_TEST(
        an_import_of_the_export_of_a_listed_brain_keeps_the_new_brain_s_own_id_and_warns_nothing);
    RUN_TEST(
        an_import_of_the_export_of_a_listed_brain_that_is_gone_takes_its_id_and_the_list_follows);
    RUN_TEST(an_import_whose_id_is_listed_twice_with_the_first_brain_live_keeps_the_new_brain_s_own_id);
    RUN_TEST(an_import_that_takes_the_file_s_id_records_it_so_a_copy_opened_first_is_the_one_flagged);
    RUN_TEST(
        an_import_whose_listed_source_brain_is_locked_past_the_busy_timeout_fails_busy_and_imports_nothing);
    RUN_TEST(an_import_with_an_unreadable_brains_json_takes_the_file_s_id);
    RUN_TEST(a_copy_that_could_not_be_told_is_a_copy_on_the_next_open);
    RUN_TEST(a_copy_whose_original_is_locked_for_less_than_the_busy_timeout_is_a_copy);
    RUN_TEST(brain_list_warns_the_first_unknown_field_with_the_c35_file_line);
    RUN_TEST(brain_list_warns_an_unknown_root_field_with_the_c35_file_line);
    RUN_TEST(brain_prune_warns_an_unknown_field_with_the_c35_file_line);
    RUN_TEST(brain_new_id_warns_an_unknown_field_in_brains_json_once);
    RUN_TEST(a_read_write_open_warns_an_unknown_field_in_brains_json);
    RUN_TEST(brain_list_json_carries_unknown_field_with_field_and_where);
    RUN_TEST(brain_list_read_of_a_file_with_an_unknown_member_survives_each_allocation_failure);
    RUN_TEST(brain_list_and_prune_that_cannot_write_the_unknown_field_warning_exit_1);
    RUN_TEST(brain_list_and_prune_json_that_cannot_write_the_data_exit_1);
    RUN_TEST(
        store_path_has_brain_id_is_1_for_the_id_a_brain_holds_0_for_another_and_minus_1_without_input);
    RUN_TEST(store_path_has_brain_id_is_0_for_a_file_that_is_not_a_brain_or_not_a_file);
    RUN_TEST(store_path_has_brain_id_cannot_tell_for_a_brain_under_an_exclusive_lock);
    RUN_TEST(store_path_has_brain_id_cannot_tell_for_a_path_it_may_not_see_or_open);
    RUN_TEST(store_path_has_brain_id_is_0_for_an_empty_file_and_a_brain_meta_with_no_row);
    RUN_TEST(store_busy_timeout_ms_reads_the_open_s_timeout);
    RUN_TEST(store_brain_set_id_replaces_the_id_and_store_brain_id_reads_it);
    RUN_TEST(store_mint_uuid_writes_a_canonical_v7_id_and_refuses_a_short_buffer);
    RUN_TEST(store_is_writable_is_true_for_a_plain_open_and_false_for_no_store);
    RUN_TEST(a_newer_brain_is_not_recorded_keeps_its_id_and_is_not_taken_for_a_copy);
    RUN_TEST(a_brain_file_that_cannot_be_written_is_not_recorded);
    RUN_TEST(a_brain_opened_through_a_symlink_is_listed_by_its_real_path);
}
