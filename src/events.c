#include "events.h"

#include "fileio.h" // IWYU pragma: keep (O_NOFOLLOW on Darwin)
#include "jsonread.h"
#include "output.h"
#include "plugin_manifest.h"
#include "settings.h"
#include "store.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h> // IWYU pragma: keep (ssize_t; IWYU 0.23 wants it, 0.21 calls it unused)
#include <unistd.h>

/* Last: under REMEMBER_TEST_HOOKS every malloc below goes through the shared
   fault hook (decision:plan18b-shared-alloc-hook). */
#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

#ifndef REMEMBER_PROCESSES
#define REMEMBER_PROCESSES 1
#endif

enum {
    HOOK_PREFIX_LEN = 7, /* "plugin/" */
    RULES_GROW = 8,
    LINE_CAP = 512,
    HOP_CAP = 320, /* one chain item as text, with room to spare */
    DEPTH_MAX = 1000000,
    LOG_MODE = 0600,
    SEGMENT_MAX = 40,
    DECIMAL = 10,
    TS_CAP = 32
};

/* ---- names ------------------------------------------------------------------------------ */

/* The two bits that mark a UTF-8 continuation byte, and their value in one. */
static const unsigned k_utf8_cont_mask = 0xC0U;
static const unsigned k_utf8_cont = 0x80U;

static const char *const k_type_names[EVENT_TYPE_COUNT] = {
    [EVENT_CREATED] = "memory.created", [EVENT_UPDATED] = "memory.updated",
    [EVENT_DELETED] = "memory.deleted", [EVENT_RESTORED] = "memory.restored",
    [EVENT_PURGED] = "memory.purged",   [EVENT_EXPIRED] = "memory.expired",
};

const char *event_type_name(EventType type)
{
    if ((unsigned)type >= (unsigned)EVENT_TYPE_COUNT) {
        return NULL;
    }
    return k_type_names[type];
}

static const char *const k_origin_names[] = {
    [STORE_ORIGIN_LOCAL] = "local",
    [STORE_ORIGIN_SYNC] = "sync",
    [STORE_ORIGIN_IMPORT] = "import",
};

/* cppcheck-suppress staticFunction ; public API (events.h); used by tests */
const char *event_origin_name(StoreOrigin origin)
{
    if ((unsigned)origin >= sizeof(k_origin_names) / sizeof(k_origin_names[0])) {
        return NULL;
    }
    return k_origin_names[origin];
}

static const char *const k_bin_names[] = {
    [STORE_BIN_LIVE] = "live",
    [STORE_BIN_EXPIRED] = "expired",
    [STORE_BIN_DELETED] = "deleted",
};

/* ---- REMEMBER_PROCESSES ------------------------------------------------------------------ */

/* ponytail: process-wide like the appio streams; one engine per process until plan 29's handle
   carries the capability bits. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_processes = REMEMBER_PROCESSES != 0;

bool events_processes(void)
{
    return g_processes;
}

void events_set_processes(bool enabled)
{
    g_processes = enabled;
}

/* ---- cause -------------------------------------------------------------------------------- */

static bool segment_char(char c, bool first)
{
    if (c >= 'a' && c <= 'z') {
        return true;
    }
    if (first) {
        return false;
    }
    if (c >= '0' && c <= '9') {
        return true;
    }
    return c == '_';
}

/* One name segment [a-z][a-z0-9_]{0,39} from s up to end. */
static bool segment_ok(const char *s, const char *end)
{
    const char *p = s;

    if (end - s < 1 || end - s > SEGMENT_MAX) {
        return false;
    }
    for (; p < end; p++) {
        if (!segment_char(*p, p == s)) {
            return false;
        }
    }
    return true;
}

/* plugin/<name> or plugin/<name>/<entry>, at most EVENT_HOOK_CAP - 1 bytes. */
static bool hook_name_ok(const char *s)
{
    const char *slash = NULL;

    if (s == NULL || strncmp(s, "plugin/", (size_t)HOOK_PREFIX_LEN) != 0) {
        return false;
    }
    s += HOOK_PREFIX_LEN;
    slash = strchr(s, '/');
    if (slash == NULL) {
        return segment_ok(s, s + strlen(s));
    }
    if (!segment_ok(s, slash)) {
        return false;
    }
    return segment_ok(slash + 1, slash + 1 + strlen(slash + 1));
}

static bool uuid_ok(const char *s)
{
    if (s == NULL) {
        return false;
    }
    return store_sync_id_is_canonical(s) != 0;
}

/* A whole integer of 1 to DEPTH_MAX: the REMEMBER_HOOK_DEPTH rule of hook_depth_blocks, with the
   value. 0 is "not in a run". */
static long long depth_from_env(void)
{
    const char *text = getenv("REMEMBER_HOOK_DEPTH"); // NOLINT(concurrency-mt-unsafe)
    char *end = NULL;
    long long value = 0;

    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return 0;
    }
    errno = 0;
    value = strtoll(text, &end, DECIMAL);
    if (errno != 0 || *end != '\0' || value < 1 || value > DEPTH_MAX) {
        return 0;
    }
    return value;
}

/* A member of an object by name, or NULL. */
static const JsonValue *member_of(const JsonValue *obj, const char *name)
{
    size_t i = 0;

    for (i = 0; i < obj->u.object.n; i++) {
        if (strcmp(obj->u.object.members[i].key, name) == 0) {
            return obj->u.object.members[i].value;
        }
    }
    return NULL;
}

static const char *string_member(const JsonValue *obj, const char *name)
{
    const JsonValue *v = member_of(obj, name);

    return (v != NULL && v->type == JSON_STRING) ? v->u.string.bytes : NULL;
}

/* One chain item the env carries: exactly hook, event_id, memory_id and depth. */
typedef struct {
    const char *hook;
    const char *event_id;
    const char *memory_id;
    long long depth;
} Hop;

static bool hop_from_json(const JsonValue *item, Hop *out)
{
    const JsonValue *depth = NULL;
    char *end = NULL;

    if (item->type != JSON_OBJECT || item->u.object.n != 4U) {
        return false;
    }
    out->hook = string_member(item, "hook");
    out->event_id = string_member(item, "event_id");
    out->memory_id = string_member(item, "memory_id");
    depth = member_of(item, "depth");
    if (!hook_name_ok(out->hook) || !uuid_ok(out->event_id) || !uuid_ok(out->memory_id) ||
        depth == NULL || depth->type != JSON_NUMBER) {
        return false;
    }
    errno = 0;
    out->depth = strtoll(depth->u.number.bytes, &end, DECIMAL);
    return errno == 0 && *end == '\0' && out->depth >= 1 && out->depth <= DEPTH_MAX;
}

static int hop_append(char *text, size_t cap, size_t *len, const Hop *hop)
{
    int n = snprintf(text + *len, cap - *len,
                     "%s{\"hook\":\"%s\",\"event_id\":\"%s\",\"memory_id\":\"%s\",\"depth\":%lld}",
                     *len > 1U ? "," : "", hop->hook, hop->event_id, hop->memory_id, hop->depth);

    if (n < 0 || (size_t)n >= cap - *len) {
        return -1;
    }
    *len += (size_t)n;
    return 0;
}

enum { CHAIN_TEXT_CAP = (EVENT_CHAIN_MAX + 1) * HOP_CAP };

/* The items of a chain array and then this run's hop: appended to text, their hooks kept in
   out->chain_hooks. 0, or -1 when an item is not valid. */
static int chain_fill(const JsonValue *array, const Hop *hop, EventCause *out, char *text)
{
    size_t len = 1U;
    size_t i = 0;

    text[0] = '[';
    text[1] = '\0';
    for (i = 0; i < array->u.array.n && i < (size_t)EVENT_CHAIN_MAX; i++) {
        Hop item;

        if (!hop_from_json(array->u.array.items[i], &item) ||
            hop_append(text, CHAIN_TEXT_CAP, &len, &item) != 0) {
            return -1;
        }
        (void)snprintf(out->chain_hooks[i], EVENT_HOOK_CAP, "%s", item.hook);
    }
    if (i != array->u.array.n || i >= (size_t)EVENT_CHAIN_MAX ||
        hop_append(text, CHAIN_TEXT_CAP, &len, hop) != 0) {
        return -1;
    }
    if (len + 2U > (size_t)CHAIN_TEXT_CAP) {
        return -1;
    }
    (void)snprintf(text + len, (size_t)CHAIN_TEXT_CAP - len, "]");
    (void)snprintf(out->chain_hooks[i], EVENT_HOOK_CAP, "%s", hop->hook);
    out->nchain = i + 1U;
    return 0;
}

/* The chain of a write made in a run: REMEMBER_HOOK_CHAIN plus this run's hop. False when
   either is missing or not valid; the cause then keeps chain []. */
static bool chain_from_env(EventCause *out, const char *hook, long long depth)
{
    // NOLINTBEGIN(concurrency-mt-unsafe) single-threaded CLI
    const char *chain_env = getenv("REMEMBER_HOOK_CHAIN");
    const char *event_id = getenv("REMEMBER_CAUSE_EVENT_ID");
    const char *memory_id = getenv("REMEMBER_ID");
    // NOLINTEND(concurrency-mt-unsafe)
    JsonValue *tree = NULL;
    Hop hop = {.hook = hook, .event_id = event_id, .memory_id = memory_id, .depth = depth};
    char *text = NULL;
    bool valid = false;

    if (chain_env == NULL || !uuid_ok(event_id) || !uuid_ok(memory_id) || hook[0] == '\0' ||
        jsonread_parse(chain_env, strlen(chain_env), &tree) != JSONREAD_OK) {
        return false;
    }
    text = (char *)malloc(CHAIN_TEXT_CAP);
    if (text != NULL && tree->type == JSON_ARRAY && chain_fill(tree, &hop, out, text) == 0) {
        free(out->chain_json);
        out->chain_json = text;
        valid = true;
    } else {
        free(text);
    }
    jsonread_free(tree);
    return valid;
}

int events_cause_read(EventCause *out)
{
    const char *hook = getenv("REMEMBER_HOOK_NAME");     // NOLINT(concurrency-mt-unsafe)
    const char *run_id = getenv("REMEMBER_HOOK_RUN_ID"); // NOLINT(concurrency-mt-unsafe)
    long long depth = depth_from_env();

    memset(out, 0, sizeof(*out));
    out->chain_json = (char *)malloc(sizeof("[]"));
    if (out->chain_json == NULL) {
        return -1;
    }
    memcpy(out->chain_json, "[]", sizeof("[]"));
    if (depth == 0) {
        return 0;
    }
    out->depth = depth;
    if (hook_name_ok(hook)) {
        (void)snprintf(out->hook, sizeof(out->hook), "%s", hook);
    }
    if (uuid_ok(run_id)) {
        (void)snprintf(out->run_id, sizeof(out->run_id), "%s", run_id);
    }
    (void)chain_from_env(out, out->hook, depth); /* not valid: chain [] stays */
    return 0;
}

void events_cause_free(EventCause *cause)
{
    free(cause->chain_json);
    cause->chain_json = NULL;
}

/* ---- rules ---------------------------------------------------------------------------------- */

void events_rules_free(EventRules *rules)
{
    free(rules->actions);
    rules->actions = NULL;
    rules->count = 0;
    rules->cap = 0;
}

/* cppcheck-suppress staticFunction ; public API (events.h); used by tests */
int events_rules_add(EventRules *rules, const PluginManifest *manifest)
{
    size_t i = 0;

    for (i = 0; i < manifest->nentries; i++) {
        const PluginEntry *entry = &manifest->entries[i];
        EventAction *slot = NULL;

        if (entry->is_filter) {
            continue;
        }
        if (rules->count == rules->cap) {
            size_t cap = rules->cap + (size_t)RULES_GROW;
            EventAction *grown =
                (EventAction *)realloc((void *)rules->actions, cap * sizeof(*grown));

            if (grown == NULL) {
                return -1;
            }
            rules->actions = grown;
            rules->cap = cap;
        }
        slot = &rules->actions[rules->count++];
        memset(slot, 0, sizeof(*slot));
        slot->entry = *entry;
        (void)snprintf(slot->plugin, sizeof(slot->plugin), "%s", manifest->name);
        (void)snprintf(slot->recipient, sizeof(slot->recipient), "plugin/%s/%s", manifest->name,
                       entry->name);
    }
    return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the user layer, then the error text
int events_rules_load(Store *store, const char *user_db, const PluginManifestList *mans,
                      EventRules *out, char *err, size_t errlen)
{
    size_t i = 0;

    for (i = 0; i < mans->count; i++) {
        char key[SETTING_KEY_MAX + 1];
        SettingRead got;

        (void)snprintf(key, sizeof(key), "plugin.%s.enabled", mans->items[i]->name);
        if (store_setting_read(store, user_db, key, NULL, &got, err, errlen) != SETTING_OK) {
            if (err[0] == '\0') {
                (void)snprintf(err, errlen, "internal error");
            }
            return -1;
        }
        if (strcmp(got.value, "true") == 0 && events_rules_add(out, mans->items[i]) != 0) {
            (void)snprintf(err, errlen, "out of memory");
            return -1;
        }
    }
    return 0;
}

/* ---- the filter ----------------------------------------------------------------------------- */

static bool event_listed(const PluginEntry *e, EventType type)
{
    size_t i = 0;

    if (e->all_events) {
        return true;
    }
    for (i = 0; i < e->nevents; i++) {
        if (strcmp(e->events[i], k_type_names[type]) == 0) {
            return true;
        }
    }
    return false;
}

/* A pattern is a stored tag, or ends in * and matches every tag that starts with the text before
   it (decision:2026-11-18-plan31-grill-filter-semantics). */
static bool tag_matches(const char *pattern, const char *tag)
{
    size_t n = strlen(pattern);

    if (n > 0U && pattern[n - 1U] == '*') {
        return strncmp(pattern, tag, n - 1U) == 0;
    }
    return strcmp(pattern, tag) == 0;
}

static bool any_tag_matches(const char *pattern, const char *const *tags, size_t ntags)
{
    size_t i = 0;

    for (i = 0; i < ntags; i++) {
        if (tag_matches(pattern, tags[i])) {
            return true;
        }
    }
    return false;
}

/* tags.any and tags.all against one tag set. An empty list is no test. */
static bool tag_set_passes(const PluginEntry *e, const char *const *tags, size_t ntags)
{
    size_t i = 0;
    bool any = e->n_tags_any == 0U;

    for (i = 0; i < e->n_tags_any && !any; i++) {
        any = any_tag_matches(e->tags_any[i], tags, ntags);
    }
    for (i = 0; i < e->n_tags_all; i++) {
        if (!any_tag_matches(e->tags_all[i], tags, ntags)) {
            return false;
        }
    }
    return any;
}

/* With match_before the tags before the write may pass it instead of the tags after. */
static bool tags_pass(const PluginEntry *e, const EventFacts *f)
{
    if (tag_set_passes(e, f->tags, f->ntags)) {
        return true;
    }
    if (e->has_match_before && e->match_before && f->has_before) {
        return tag_set_passes(e, f->tags_before, f->ntags_before);
    }
    return false;
}

/* The names of one filter list: n entries of cap bytes each. An empty list is no test. */
typedef struct {
    const char *items;
    size_t cap;
    size_t count;
} NameList;

static bool name_listed(NameList list, const char *name)
{
    size_t i = 0;

    if (list.count == 0U) {
        return true;
    }
    for (i = 0; i < list.count; i++) {
        if (strcmp(list.items + (i * list.cap), name) == 0) {
            return true;
        }
    }
    return false;
}

/* A not_caused_by string is plugin/<name>/<entry>, or a bare plugin name, which names every
   entry of that plugin (decision:2026-11-18-plan31-grill-filter-semantics). */
static bool hook_is_named(const char *pattern, const char *hook)
{
    size_t n = 0;

    if (strchr(pattern, '/') != NULL) {
        return strcmp(pattern, hook) == 0;
    }
    n = strlen(pattern);
    if (strncmp(hook, "plugin/", (size_t)HOOK_PREFIX_LEN) != 0 ||
        strncmp(hook + HOOK_PREFIX_LEN, pattern, n) != 0) {
        return false;
    }
    if (hook[HOOK_PREFIX_LEN + n] == '\0') {
        return true;
    }
    return hook[HOOK_PREFIX_LEN + n] == '/';
}

/* Whether a hook of the chain is one pattern names. */
static bool chain_names(const char *pattern, const EventCause *cause)
{
    size_t i = 0;

    for (i = 0; i < cause->nchain; i++) {
        if (hook_is_named(pattern, cause->chain_hooks[i])) {
            return true;
        }
    }
    return false;
}

static bool caused_by(const PluginEntry *e, const EventCause *cause)
{
    size_t i = 0;

    for (i = 0; i < e->n_not_caused_by; i++) {
        if (cause->hook[0] != '\0' && hook_is_named(e->not_caused_by[i], cause->hook)) {
            return true;
        }
        if (chain_names(e->not_caused_by[i], cause)) {
            return true;
        }
    }
    return false;
}

EventMatch events_match(const EventAction *action, const EventFacts *f)
{
    const PluginEntry *e = &action->entry;
    bool origin_ok = false;
    size_t i = 0;

    const NameList created = {&e->created_by[0][0], PLUGIN_ACTOR_CAP, e->n_created_by};
    const NameList updated = {&e->updated_by[0][0], PLUGIN_ACTOR_CAP, e->n_updated_by};
    const NameList bins = {&e->bins[0][0], PLUGIN_BIN_CAP, e->n_bins};

    if (!event_listed(e, f->type) || !tags_pass(e, f) ||
        !name_listed(created, f->created_by_kind) || !name_listed(updated, f->updated_by_kind) ||
        !name_listed(bins, k_bin_names[f->bin]) || caused_by(e, f->cause)) {
        return EVENT_SKIP_FILTER;
    }
    for (i = 0; i < e->n_origins; i++) {
        if (strcmp(e->origins[i], k_origin_names[f->origin]) == 0) {
            origin_ok = true;
        }
    }
    if (!origin_ok && f->hooks != STORE_HOOKS_ON) {
        return EVENT_SKIP_ORIGIN;
    }
    return EVENT_MATCH;
}

void events_count_skip(EventAction *action, EventMatch match, EventType type)
{
    if (match == EVENT_SKIP_FILTER) {
        action->skipped_filter++;
    } else if (match == EVENT_SKIP_ORIGIN) {
        action->skipped_source[type]++;
    }
}

/* ---- planning ---------------------------------------------------------------------------------
 */

static bool same_text(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    return strcmp(a, b) == 0;
}

static bool same_tags(const Entry *a, const Entry *b)
{
    size_t i = 0;

    if (a->ntags != b->ntags) {
        return false;
    }
    for (i = 0; i < a->ntags; i++) {
        if (strcmp(a->tags[i], b->tags[i]) != 0) {
            return false;
        }
    }
    return true;
}

static bool same_link(const EventLink *a, const EventLink *b)
{
    if (strcmp(a->kind, b->kind) != 0 || strcmp(a->direction, b->direction) != 0) {
        return false;
    }
    return strcmp(a->other_id, b->other_id) == 0;
}

static bool link_in(const EventLink *list, size_t n, const EventLink *link)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        if (same_link(&list[i], link)) {
            return true;
        }
    }
    return false;
}

static bool draft_push(EventDraft **list, size_t *n, size_t *cap, const EventDraft *draft)
{
    if (*n == *cap) {
        size_t ncap = (*cap == 0U) ? (size_t)RULES_GROW : *cap * 2U;
        EventDraft *grown = (EventDraft *)realloc((void *)*list, ncap * sizeof(*grown));

        if (grown == NULL) {
            return false;
        }
        *list = grown;
        *cap = ncap;
    }
    (*list)[(*n)++] = *draft;
    return true;
}

/* The one type a row's before and after give, or EVENT_UPDATED when only fields and links may
   have changed. */
static EventType type_of(const EventRow *row)
{
    bool was_deleted = false;
    bool is_deleted = false;

    if (row->before == NULL) {
        return EVENT_CREATED;
    }
    if (row->after == NULL) {
        return EVENT_PURGED;
    }
    was_deleted = row->before->deleted_at != NULL;
    is_deleted = row->after->deleted_at != NULL;
    if (!was_deleted && is_deleted) {
        return EVENT_DELETED;
    }
    if (was_deleted && !is_deleted) {
        return EVENT_RESTORED;
    }
    return EVENT_UPDATED;
}

static bool plan_links(const EventRow *row, EventDraft **list, size_t *n, size_t *cap)
{
    size_t i = 0;
    EventDraft d;

    memset(&d, 0, sizeof(d));
    d.type = EVENT_UPDATED;
    d.has_link = true;
    for (i = 0; i < row->nlinks_after; i++) {
        d.link = row->links_after[i];
        if (!link_in(row->links_before, row->nlinks_before, &d.link) &&
            !draft_push(list, n, cap, &d)) {
            return false;
        }
    }
    for (i = 0; i < row->nlinks_before; i++) {
        d.link = row->links_before[i];
        if (!link_in(row->links_after, row->nlinks_after, &d.link) &&
            !draft_push(list, n, cap, &d)) {
            return false;
        }
    }
    return true;
}

bool events_plan(const EventRow *row, EventDraft **out, size_t *n)
{
    EventDraft *list = NULL;
    size_t cap = 0;
    EventDraft d;
    EventType type = type_of(row);

    memset(&d, 0, sizeof(d));
    *out = NULL;
    *n = 0;
    if (row->before == NULL && row->after == NULL) {
        return true; /* a row that came and went in one write: nobody saw it */
    }
    d.type = type;
    if (type != EVENT_UPDATED) {
        if (!draft_push(&list, n, &cap, &d)) {
            return false;
        }
        *out = list;
        return true;
    }
    d.body_changed = strcmp(row->before->body, row->after->body) != 0;
    if (!same_text(row->before->key, row->after->key)) {
        d.key_changed = true;
    }
    if (!same_tags(row->before, row->after)) {
        d.tags_changed = true;
    }
    if (!same_text(row->before->expires_at, row->after->expires_at)) {
        d.expires_changed = true;
    }
    if ((d.body_changed || d.key_changed || d.tags_changed || d.expires_changed) &&
        !draft_push(&list, n, &cap, &d)) {
        return false;
    }
    if (!plan_links(row, &list, n, &cap)) {
        free(list);
        *n = 0;
        return false;
    }
    *out = list;
    return true;
}

/* ---- the payload ------------------------------------------------------------------------------
 */

/* Which cuts of H-3.4 were made, in order. */
typedef struct {
    bool before_body;
    bool links;
    bool body;
} Cuts;

typedef struct {
    const EventStamp *stamp;
    const EventDraft *draft;
    const EventRow *row;
    const EventMemory *memory;
} PayloadIn;

static int write_nullable_string(FILE *f, const char *s)
{
    if (s == NULL) {
        return fputs("null", f) < 0 ? -1 : 0;
    }
    return output_json_string(f, s);
}

/* The comma that goes before every item of a list or object but the first. */
static int write_sep(FILE *f, bool *first)
{
    int rc = 0;

    if (!*first && fputc(',', f) == EOF) {
        rc = -1;
    }
    *first = false;
    return rc;
}

static bool has_tag(const Entry *e, const char *tag)
{
    size_t i = 0;

    for (i = 0; i < e->ntags; i++) {
        if (strcmp(e->tags[i], tag) == 0) {
            return true;
        }
    }
    return false;
}

/* tags that are in a and not in b. */
static int write_tag_difference(FILE *f, const Entry *a, const Entry *b)
{
    size_t i = 0;
    bool first = true;

    if (fputc('[', f) == EOF) {
        return -1;
    }
    for (i = 0; i < a->ntags; i++) {
        if (has_tag(b, a->tags[i])) {
            continue;
        }
        if (write_sep(f, &first) != 0 || output_json_string(f, a->tags[i]) != 0) {
            return -1;
        }
    }
    return fputc(']', f) == EOF ? -1 : 0;
}

/* One name in the "fields" list. */
static int write_field_name(FILE *f, bool *first, const char *name)
{
    if (write_sep(f, first) != 0) {
        return -1;
    }
    return fprintf(f, "\"%s\"", name) < 0 ? -1 : 0;
}

static int write_fields_list(FILE *f, const EventDraft *d)
{
    bool first = true;

    if (fputc('[', f) == EOF) {
        return -1;
    }
    if ((d->key_changed && write_field_name(f, &first, "key") != 0) ||
        (d->body_changed && write_field_name(f, &first, "body") != 0) ||
        (d->tags_changed && write_field_name(f, &first, "tags") != 0) ||
        (d->expires_changed && write_field_name(f, &first, "expires_at") != 0)) {
        return -1;
    }
    return fputc(']', f) == EOF ? -1 : 0;
}

/* The start of one member of the "before" object: its comma and its name. */
static int write_member(FILE *f, bool *first, const char *name)
{
    if (write_sep(f, first) != 0) {
        return -1;
    }
    return fprintf(f, "\"%s\":", name) < 0 ? -1 : 0;
}

/* The old value of each changed field, as one object member each. */
static int write_before(FILE *f, const PayloadIn *in, Cuts cuts)
{
    const EventDraft *d = in->draft;
    const Entry *old = in->row->before;
    bool first = true;

    if (fputc('{', f) == EOF) {
        return -1;
    }
    if (d->key_changed &&
        (write_member(f, &first, "key") != 0 || write_nullable_string(f, old->key) != 0)) {
        return -1;
    }
    if (d->body_changed && !cuts.before_body &&
        (write_member(f, &first, "body") != 0 || output_json_string(f, old->body) != 0)) {
        return -1;
    }
    if (d->tags_changed &&
        (write_member(f, &first, "tags") != 0 ||
         output_json_string_array(f, (const char *const *)old->tags, old->ntags) != 0)) {
        return -1;
    }
    if (d->expires_changed && (write_member(f, &first, "expires_at") != 0 ||
                               write_nullable_string(f, old->expires_at) != 0)) {
        return -1;
    }
    return fputc('}', f) == EOF ? -1 : 0;
}

static int write_link_change(FILE *f, const EventLink *link)
{
    if (fputs("{\"link\":{\"kind\":", f) < 0 || output_json_string(f, link->kind) != 0 ||
        fputs(",\"direction\":", f) < 0 || output_json_string(f, link->direction) != 0 ||
        fputs(",\"other\":{\"id\":", f) < 0 || output_json_string(f, link->other_id) != 0 ||
        fputs(",\"key\":", f) < 0 || write_nullable_string(f, link->other_key) != 0) {
        return -1;
    }
    return fputs("}}}", f) < 0 ? -1 : 0;
}

static int write_field_changes(FILE *f, const PayloadIn *in, Cuts cuts)
{
    if (fputs("{\"fields\":", f) < 0 || write_fields_list(f, in->draft) != 0 ||
        fputs(",\"before\":", f) < 0 || write_before(f, in, cuts) != 0 ||
        fputs(",\"tags_added\":", f) < 0 ||
        write_tag_difference(f, in->row->after, in->row->before) != 0 ||
        fputs(",\"tags_removed\":", f) < 0 ||
        write_tag_difference(f, in->row->before, in->row->after) != 0) {
        return -1;
    }
    return fputc('}', f) == EOF ? -1 : 0;
}

static int write_changes(FILE *f, const PayloadIn *in, Cuts cuts)
{
    if (in->draft->type != EVENT_UPDATED) {
        return fputs("null", f) < 0 ? -1 : 0;
    }
    if (in->draft->has_link) {
        return write_link_change(f, &in->draft->link);
    }
    return write_field_changes(f, in, cuts);
}

static int write_cause(FILE *f, const EventStamp *st)
{
    const EventCause *c = st->cause;

    if (fputs("{\"command\":", f) < 0 || output_json_string(f, st->command) != 0 ||
        fprintf(f, ",\"origin\":\"%s\",\"hook\":", event_origin_name(st->origin)) < 0 ||
        write_nullable_string(f, c->hook[0] != '\0' ? c->hook : NULL) != 0 ||
        fputs(",\"run_id\":", f) < 0 ||
        write_nullable_string(f, c->run_id[0] != '\0' ? c->run_id : NULL) != 0) {
        return -1;
    }
    return fprintf(f, ",\"depth\":%lld,\"chain\":%s}", c->depth, c->chain_json) < 0 ? -1 : 0;
}

static int write_truncation(FILE *f, Cuts cuts)
{
    bool first = true;

    if (fprintf(f, ",\"truncated\":%s,\"truncated_fields\":[",
                (cuts.before_body || cuts.links || cuts.body) ? "true" : "false") < 0) {
        return -1;
    }
    if ((cuts.before_body && write_field_name(f, &first, "changes.before.body") != 0) ||
        (cuts.links && write_field_name(f, &first, "memory.links") != 0) ||
        (cuts.body && write_field_name(f, &first, "memory.body") != 0)) {
        return -1;
    }
    return fputc(']', f) == EOF ? -1 : 0;
}

static int write_data(FILE *f, const PayloadIn *in, Cuts cuts, const Entry *memory)
{
    const EventStamp *st = in->stamp;
    EventLinks links = {.rows = in->memory->links, .count = in->memory->nlinks};

    if (cuts.links) {
        links = (EventLinks){.rows = NULL, .count = 0U};
    }
    if (fputs("{\"event_id\":", f) < 0 || output_json_string(f, st->event_id) != 0 ||
        fprintf(f, ",\"event_type\":\"%s\",\"occurred_at\":\"%s\",\"detected_at\":\"%s\"",
                event_type_name(in->draft->type), st->at, st->at) < 0 ||
        fputs(",\"batch_id\":", f) < 0 || output_json_string(f, st->batch_id) != 0 ||
        fputs(",\"cause\":", f) < 0 || write_cause(f, st) != 0 ||
        fputs(",\"brain\":{\"path\":", f) < 0 || output_json_string(f, st->brain_path) != 0 ||
        fputs(",\"device_id\":", f) < 0 || output_json_string(f, st->device_id) != 0 ||
        fputs("},\"memory\":", f) < 0 ||
        output_event_memory_json(f, memory, in->memory->change_seq, links, st->at) != 0 ||
        fputs(",\"changes\":", f) < 0 || write_changes(f, in, cuts) != 0 ||
        write_truncation(f, cuts) != 0) {
        return -1;
    }
    return fprintf(f, ",\"limits\":{\"max_bytes\":%d}}", EVENT_PAYLOAD_MAX) < 0 ? -1 : 0;
}

/* The data text for these cuts, heap. NULL when the text cannot be made. */
static char *render(const PayloadIn *in, Cuts cuts)
{
    Entry cut = *in->memory->entry;
    char *piece = NULL;
    char *buf = NULL;
    size_t len = 0;
    FILE *f = NULL;
    int rc = 0;

    if (cuts.body) {
        size_t keep = strlen(in->memory->entry->body);

        keep = keep > (size_t)EVENT_BODY_CUT ? (size_t)EVENT_BODY_CUT : keep;
        while (keep > 0U && ((unsigned)(unsigned char)in->memory->entry->body[keep] &
                             k_utf8_cont_mask) == k_utf8_cont) {
            keep--;
        }
        piece = (char *)malloc(keep + 1U);
        if (piece == NULL) {
            return NULL;
        }
        memcpy(piece, in->memory->entry->body, keep);
        piece[keep] = '\0';
        cut.body = piece;
    }
    f = open_memstream(&buf, &len);
    if (f == NULL) {
        free(piece);
        return NULL;
    }
    rc = write_data(f, in, cuts, &cut);
    free(piece);
    if (fclose(f) != 0 || rc != 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

char *events_payload(const EventStamp *stamp, const EventDraft *draft, const EventRow *row,
                     const EventMemory *memory)
{
    PayloadIn in = {.stamp = stamp, .draft = draft, .row = row, .memory = memory};
    Cuts cuts = {.before_body = false, .links = false, .body = false};
    char *text = render(&in, cuts);
    bool can_before = false;
    bool can_links = memory->nlinks > 0U;
    bool can_body = strlen(memory->entry->body) > (size_t)EVENT_BODY_CUT;

    if (draft->type == EVENT_UPDATED && !draft->has_link && draft->body_changed) {
        can_before = true;
    }

    if (text != NULL && strlen(text) > (size_t)EVENT_PAYLOAD_MAX && can_before) {
        free(text);
        cuts.before_body = true;
        text = render(&in, cuts);
    }
    if (text != NULL && strlen(text) > (size_t)EVENT_PAYLOAD_MAX && can_links) {
        free(text);
        cuts.links = true;
        text = render(&in, cuts);
    }
    if (text != NULL && strlen(text) > (size_t)EVENT_PAYLOAD_MAX && can_body) {
        free(text);
        cuts.body = true;
        text = render(&in, cuts);
    }
    return text;
}

char *events_recipients_json(const char *const *entries, size_t count)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    size_t i = 0;
    int rc = 0;

    if (f == NULL) {
        return NULL;
    }
    rc = fputc('[', f) == EOF ? -1 : 0;
    for (i = 0; rc == 0 && i < count; i++) {
        rc = fprintf(f, "%s{\"entry\":\"%s\",\"state\":\"pending\"}", i > 0U ? "," : "",
                     entries[i]) < 0
                 ? -1
                 : 0;
    }
    rc = (rc == 0 && fputc(']', f) != EOF) ? 0 : -1;
    if (fclose(f) != 0 || rc != 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* ---- hooks.log --------------------------------------------------------------------------------
 */

/* One H-7.2 line, members in the order the log fixes. Every string is a name this program holds:
   a command word, an event type, a plugin or entry name. */
static int skipped_line(char *out, size_t cap, const char *entry_fields, const char *const *words)
{
    char at[TS_CAP];

    if (utc_now(at, sizeof(at)) != 0) {
        return -1;
    }
    return snprintf(out, cap,
                    "{\"version\":1,\"at\":\"%s\",\"event\":\"skipped_source\","
                    "\"event_type\":\"%s\",\"command\":\"%s\",\"origin\":\"%s\",%s}\n",
                    at, words[0], words[1], words[2], entry_fields);
}

static bool log_append(int fd, const char *line, size_t len)
{
    return write(fd, line, len) == (ssize_t)len;
}

static bool has_skipped_source(const EventAction *action)
{
    size_t type = 0;

    for (type = 0; type < (size_t)EVENT_TYPE_COUNT; type++) {
        if (action->skipped_source[type] != 0U) {
            return true;
        }
    }
    return false;
}

/* Whether any action has an origins-only miss to report. */
static bool any_skipped_source(const EventRules *rules)
{
    size_t i = 0;

    for (i = 0; i < rules->count; i++) {
        if (has_skipped_source(&rules->actions[i])) {
            return true;
        }
    }
    return false;
}

/* One H-7.2 line for this action and type, appended to the open log. */
static bool log_one(int fd, const EventAction *a, int type, const char *const *words)
{
    char fields[LINE_CAP];
    char line[LINE_CAP * 2];
    int n = snprintf(fields, sizeof(fields), "\"plugin\":\"%s\",\"action\":\"%s\",\"count\":%zu",
                     a->plugin, a->entry.name, a->skipped_source[type]);

    if (n < 0 || (size_t)n >= sizeof(fields)) {
        return false;
    }
    n = skipped_line(line, sizeof(line), fields, words);
    if (n < 0) {
        return false;
    }
    return log_append(fd, line, (size_t)n);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain path, then the command word
size_t events_log_skipped(EventRules *rules, const char *brain_path, const char *command,
                          StoreOrigin origin)
{
    char path[PATH_MAX];
    struct stat st;
    size_t written = 0;
    size_t i = 0;
    int fd = -1;

    if (origin == STORE_ORIGIN_LOCAL || !any_skipped_source(rules) ||
        snprintf(path, sizeof(path), "%s.d/hooks.log", brain_path) >= (int)sizeof(path)) {
        return 0;
    }
    /* O_NONBLOCK: a FIFO at the path fails the open (no reader) or is refused below; it never
       hangs a write that has already committed. */
    fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, LOG_MODE);
    if (fd < 0) {
        return 0;
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        (void)close(fd);
        return 0;
    }
    for (i = 0; i < rules->count; i++) {
        EventAction *a = &rules->actions[i];
        int type = 0;

        for (type = 0; type < EVENT_TYPE_COUNT; type++) {
            const char *words[] = {k_type_names[type], command, k_origin_names[origin]};

            if (a->skipped_source[type] != 0U && log_one(fd, a, type, words)) {
                a->skipped_source[type] = 0;
                written++;
            }
        }
    }
    (void)close(fd);
    return written;
}
