#ifndef REMEMBER_EVENTS_H
#define REMEMBER_EVENTS_H

#include "plugin_manifest.h"
#include "plugin_settings.h"
#include "store.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Memory events and the outbox row (plan 31; specs/EVENTS.md). A write that changes a memory
 * makes its events in the write transaction, and inserts one outbox row per event when an
 * enabled action's whole filter matches. Nothing here starts a process.
 *
 * This module has no SQL and no sqlite. store_sqlite.c finds what a write changed (temp
 * triggers keep the first image of each memory the write touches), hands each memory to
 * events_plan, and inserts the rows. The outbox table is the v6 table of 010 §17: it is
 * written only in store_sqlite.c.
 */

enum {
    EVENT_PAYLOAD_MAX = 262144, /* H-3.4: bytes of data */
    EVENT_BODY_CUT = 16384,     /* H-3.4: body bytes kept past the limit */
    EVENT_UUID_LEN = 36,
    EVENT_CHAIN_MAX = 100,
    /* "plugin/" + a name + "/" + an entry name + NUL */
    EVENT_HOOK_CAP = 7 + PLUGIN_NAME_CAP + 1 + PLUGIN_NAME_CAP + 1,
    EVENT_CAUSE_PAD = 10,
    EVENT_CAUSE_PAD2 = 4
};

/* The six event types, in the order of the protocol's enum (R3.3). */
typedef enum {
    EVENT_CREATED = 0,
    EVENT_UPDATED,
    EVENT_DELETED,
    EVENT_RESTORED,
    EVENT_PURGED,
    EVENT_EXPIRED,
    EVENT_TYPE_COUNT
} EventType;

/* "memory.created" and the others. NULL for a value outside the enum. */
const char *event_type_name(EventType type);

/* "local", "sync" or "import". NULL for a value outside the enum. */
const char *event_origin_name(StoreOrigin origin);

/* What caused a write (data.cause, R3.2;
   decision:2026-11-18-plan31-grill-chain-env-conflict-accept). hook and run_id are "" when none.
   chain_json is the array text, "[]" for none. */
struct EventCause {
    char hook[EVENT_HOOK_CAP];
    char run_id[EVENT_UUID_LEN + 1];
    char pad_[EVENT_CAUSE_PAD];
    long long depth;
    size_t nchain;
    char chain_hooks[EVENT_CHAIN_MAX][EVENT_HOOK_CAP];
    char pad2_[EVENT_CAUSE_PAD2];
    char *chain_json;
};

/* Read the cause of this write from the environment (H-5.2, H-5.3). Outside a run (no
   REMEMBER_HOOK_DEPTH of 1 or more) the cause is a direct write: no hook, depth 0, chain
   []. Inside one: hook is REMEMBER_HOOK_NAME, run_id REMEMBER_HOOK_RUN_ID, depth
   REMEMBER_HOOK_DEPTH, and chain is REMEMBER_HOOK_CHAIN (a JSON array of
   {hook, event_id, memory_id, depth}) plus the hop {hook, REMEMBER_CAUSE_EVENT_ID,
   REMEMBER_ID, depth}. A variable that is missing or not valid is left out: a bad
   REMEMBER_HOOK_NAME or REMEMBER_HOOK_RUN_ID reads as none, and a chain or hop that is not
   valid gives chain [] (the write counts as depth 1). 0, or -1 on out of memory. */
int events_cause_read(EventCause *out);
void events_cause_free(EventCause *cause);

/* One enabled action of an installed plugin: its manifest entry, and what its filter let
   through or kept out so far in this process. */
typedef struct {
    PluginEntry entry;
    char plugin[PLUGIN_NAME_CAP + 1];
    char recipient[EVENT_HOOK_CAP]; /* plugin/<name>/<entry> */
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[6]; /* explicit tail padding (kept -Wpadded-clean) */
    size_t skipped_filter;
    size_t skipped_source[EVENT_TYPE_COUNT]; /* by event type, origins only */
} EventAction;

struct EventRules {
    EventAction *actions;
    size_t count;
    size_t cap;
};

void events_rules_free(EventRules *rules);

/* Add every action (not filter) of the manifest. Call it for an enabled plugin only. 0, or -1
   on out of memory (rules then hold what was added before). */
int events_rules_add(EventRules *rules, const PluginManifest *manifest);

/* The rules of the enabled plugins among mans: the actions of each one whose
   plugin.<name>.enabled is true for this brain (the settings of store, with user_db as the user
   layer, or NULL for none). 0, or -1 when memory runs out or a setting cannot be read. */
int events_rules_load(Store *store, const char *user_db, const PluginManifestList *mans,
                      EventRules *out);

/* What the filter says about one event of one memory. */
typedef enum { EVENT_MATCH = 0, EVENT_SKIP_FILTER, EVENT_SKIP_ORIGIN } EventMatch;

/* The memory after the write (the last one, for a purge) and before it, as the filter reads them.
   bin is the bin the memory had before the write: a new memory has none, so its own bin. The
   kinds are the actor kinds of created_by and updated_by after the write. */
typedef struct {
    const char *const *tags;
    const char *const *tags_before; /* read only when has_before */
    const char *created_by_kind;
    const char *updated_by_kind;
    const EventCause *cause;
    size_t ntags;
    size_t ntags_before;
    EventType type;
    StoreOrigin origin;
    StoreBin bin;
    StoreHooks hooks; /* ON skips the origins test (H-4.3) */
    bool has_before;  /* false: the memory is new */
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} EventFacts;

/* The whole filter of one action (H-4.1; decision:2026-11-18-plan31-grill-filter-semantics):
   events, then tags, created_by, updated_by, bins and not_caused_by, then origins. A miss before
   origins is EVENT_SKIP_FILTER. EVENT_SKIP_ORIGIN means only the origins test missed. */
EventMatch events_match(const EventAction *action, const EventFacts *facts);

/* A miss of events_match, counted on the action. */
void events_count_skip(EventAction *action, EventMatch match, EventType type);

/* The links of a memory the diff compares: borrowed strings. kind is related, supersedes or
   cites; direction is outgoing, incoming or both (related). */
typedef struct {
    const char *kind;
    const char *direction;
    const char *other_id;
    const char *other_key; /* NULL for a keyless memory */
} EventLink;

/* One touched memory: how it was before the write (NULL when new) and after (NULL when the
   row is gone), and its live links in both states. */
typedef struct {
    const Entry *before;
    const Entry *after;
    const EventLink *links_before;
    const EventLink *links_after;
    size_t nlinks_before;
    size_t nlinks_after;
} EventRow;

/* One event to write. For EVENT_UPDATED, the flags name the fields that changed, or has_link
   says a link did (then link points into the row's arrays). */
typedef struct {
    EventLink link;
    EventType type;
    bool has_link;
    bool body_changed;
    bool key_changed;
    bool tags_changed;
    bool expires_changed;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} EventDraft;

/* The events of one touched memory (H-1, H-1.3): created for a new row, purged for a removed one,
   deleted or restored when deleted_at changed, else one updated for the fields and one for each
   link that came or went. None when nothing the event list names changed. *out is heap
   (free it); *n may be 0. False on out of memory. */
bool events_plan(const EventRow *row, EventDraft **out, size_t *n);

/* Who and when for one event. */
typedef struct {
    const char *event_id;
    const char *batch_id;
    const char *at; /* occurred_at, detected_at and the bin clock */
    const char *brain_path;
    const char *device_id;
    const char *command;
    const EventCause *cause;
    StoreOrigin origin;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} EventStamp;

/* The memory an event carries: the row after the write (the last one for a purge), the stubs of
   its links (rows whose subject_id is entry->id) and its change_seq. */
typedef struct {
    const Entry *entry;
    const StoreNeighbor *links;
    size_t nlinks;
    long long change_seq;
} EventMemory;

/* The frozen data of one event: H-3.2 without plugin, entry, data_dir and settings (plan 35
   adds those for each recipient). Past EVENT_PAYLOAD_MAX bytes the cuts of H-3.4 apply in order,
   each only when it removes something. Heap text, NULL on out of memory. */
char *events_payload(const EventStamp *stamp, const EventDraft *draft, const EventRow *row,
                     const EventMemory *memory);

/* [{"entry":"plugin/<name>/<entry>","state":"pending"},...] for the matched actions. Heap
   text, NULL on out of memory. */
char *events_recipients_json(const char *const *entries, size_t count);

/* Append one skipped_source line (H-7.2) to <brain>.d/hooks.log for each action and event type
   that origins alone kept out, then zero those counters. Only an origin of sync or import is
   logged. Returns the number of lines written; a log that cannot be written is not an error
   for the command (the line is advisory), so it counts none. */
size_t events_log_skipped(EventRules *rules, const char *brain_path, const char *command,
                          StoreOrigin origin);

/* REMEMBER_PROCESSES: false in a build that cannot start programs (H-0.7). The default is the
   compile-time REMEMBER_PROCESSES (1 unless the build sets 0); the setter is for the host and
   the tests (plan 29 feeds it from REMEMBER_CAP_PROCESSES). */
bool events_processes(void);
void events_set_processes(bool enabled);

#endif /* REMEMBER_EVENTS_H */
