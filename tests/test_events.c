#include "events.h"
#include "harness.h"
#include "output.h"
#ifdef REMEMBER_PLUGINS_DIAG_TEST
#include "remember_app.h"
#endif
#include "plugin_manifest.h"
#include "plugin_settings.h"
#include "register.h"
#include "settings.h"
#include "store.h"
#include "test.h"
#include "test_alloc.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Counts and sizes below are the limits and ids under test. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/* Plan 31 (specs/EVENTS.md). Decisions: decision:2026-11-17-plan31-grill-purged-processes-cause,
   decision:2026-11-18-plan31-grill-filter-semantics,
   decision:2026-11-18-plan31-grill-link-direction-batch-id,
   decision:2026-11-18-plan31-grill-chain-env-conflict-accept,
   decision:2026-11-08-sync-record-order-hooks-delivery, decision:s-14. */

static const char k_event_id[] = "01a14521-0a00-7c11-9a00-0000000000e1";
static const char k_batch_id[] = "01a14521-0b00-7c11-9a00-0000000000b1";
static const char k_memory_id[] = "01a14521-09ff-7d20-8c00-00000000aa01";
static const char k_other_id[] = "01a14521-09ff-7d20-8c00-00000000aa02";
static const char k_device[] = "01a0d90a-e71c-7a0a-bb2c-3b377482fa51";
static const char k_at[] = "2026-11-18T10:00:00.000Z";
static const char k_actor_human[] = "{\"kind\":\"human\",\"id\":null}";
static const char k_actor_agent[] = "{\"kind\":\"agent\",\"id\":null}";
static const char k_vv[] = "{\"01a0d90a-e71c-7a0a-bb2c-3b377482fa51\":1}";

/* Entry.tags is char **: these tables are never written. */
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static char *g_tags_ab[] = {(char *)"alpha", (char *)"beta"};
static char *g_tags_abc[] = {(char *)"alpha", (char *)"beta", (char *)"gamma"};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

static Entry given_memory(void)
{
    return (Entry){.id = 7,
                   .sync_id = (char *)k_memory_id,
                   .key = NULL,
                   .body = (char *)"hello",
                   .tags = g_tags_ab,
                   .ntags = 2U,
                   .created_by = (char *)k_actor_human,
                   .updated_by = (char *)k_actor_human,
                   .created_at = (char *)k_at,
                   .updated_at = (char *)k_at,
                   .expires_at = NULL,
                   .deleted_at = NULL,
                   .version_vector = (char *)k_vv};
}

static EventCause given_direct_cause(void)
{
    EventCause c;

    ASSERT_EQ_INT(events_cause_read(&c), 0);
    return c;
}

static const EventLink k_link_related = {
    .kind = "related", .direction = "both", .other_id = k_other_id, .other_key = "other:key"};
static const EventLink k_link_out = {
    .kind = "cites", .direction = "outgoing", .other_id = k_other_id, .other_key = NULL};

/* ---- names ---------------------------------------------------------------------------------- */

TEST(event_type_and_origin_names)
{
    ASSERT_STREQ(event_type_name(EVENT_CREATED), "memory.created");
    ASSERT_STREQ(event_type_name(EVENT_UPDATED), "memory.updated");
    ASSERT_STREQ(event_type_name(EVENT_DELETED), "memory.deleted");
    ASSERT_STREQ(event_type_name(EVENT_RESTORED), "memory.restored");
    ASSERT_STREQ(event_type_name(EVENT_PURGED), "memory.purged");
    ASSERT_STREQ(event_type_name(EVENT_EXPIRED), "memory.expired");
    ASSERT_TRUE(event_type_name(EVENT_TYPE_COUNT) == NULL);
    ASSERT_STREQ(event_origin_name(STORE_ORIGIN_LOCAL), "local");
    ASSERT_STREQ(event_origin_name(STORE_ORIGIN_SYNC), "sync");
    ASSERT_STREQ(event_origin_name(STORE_ORIGIN_IMPORT), "import");
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the point: a value past the enum
    ASSERT_TRUE(event_origin_name((StoreOrigin)3) == NULL);
}

/* ---- planning ------------------------------------------------------------------------------- */

typedef struct {
    EventDraft *drafts;
    size_t n;
} Plan;

static void plan_free(Plan *plan)
{
    free(plan->drafts);
    plan->drafts = NULL;
    plan->n = 0U;
}

static Plan given_plan(const EventRow *row)
{
    Plan p = {.drafts = NULL, .n = 0U};

    ASSERT_TRUE(events_plan(row, &p.drafts, &p.n));
    return p;
}

TEST(plan_a_new_row_is_one_created_event)
{
    Entry after = given_memory();
    Plan p = given_plan(&(EventRow){.before = NULL, .after = &after});

    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_CREATED);
    ASSERT_FALSE(p.drafts[0].has_link);
    plan_free(&p);
}

TEST(plan_a_new_deleted_row_is_still_created)
{
    Entry after = given_memory();
    Plan p;

    after.deleted_at = (char *)k_at;
    p = given_plan(&(EventRow){.before = NULL, .after = &after});
    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_CREATED);
    plan_free(&p);
}

TEST(plan_a_removed_row_is_one_purged_event)
{
    Entry before = given_memory();
    Plan p = given_plan(&(EventRow){.before = &before, .after = NULL});

    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_PURGED);
    plan_free(&p);
}

TEST(plan_a_row_that_came_and_went_in_one_write_is_no_event)
{
    Plan p = given_plan(&(EventRow){.before = NULL, .after = NULL});

    ASSERT_EQ_INT(p.n, 0);
    ASSERT_TRUE(p.drafts == NULL);
}

TEST(plan_deleted_at_set_is_deleted_and_cleared_is_restored)
{
    Entry live = given_memory();
    Entry gone = given_memory();
    Plan p;

    gone.deleted_at = (char *)k_at;
    p = given_plan(&(EventRow){.before = &live, .after = &gone});
    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_DELETED);
    plan_free(&p);
    p = given_plan(&(EventRow){.before = &gone, .after = &live});
    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_RESTORED);
    plan_free(&p);
}

TEST(plan_a_delete_that_also_changes_fields_is_only_deleted)
{
    Entry live = given_memory();
    Entry gone = given_memory();
    Plan p;

    gone.deleted_at = (char *)k_at;
    gone.body = (char *)"other body";
    gone.expires_at = (char *)"2030-01-01T00:00:00.000Z";
    p = given_plan(&(EventRow){.before = &live, .after = &gone});
    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_DELETED);
    plan_free(&p);
}

/* One row each: only the named flag is set, so each field comparison is read alone. */
static void assert_one_field(const Entry *before, const Entry *after, EventDraft want)
{
    Plan p = given_plan(&(EventRow){.before = before, .after = after});

    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_UPDATED);
    ASSERT_FALSE(p.drafts[0].has_link);
    ASSERT_EQ_INT(p.drafts[0].body_changed, want.body_changed);
    ASSERT_EQ_INT(p.drafts[0].key_changed, want.key_changed);
    ASSERT_EQ_INT(p.drafts[0].tags_changed, want.tags_changed);
    ASSERT_EQ_INT(p.drafts[0].expires_changed, want.expires_changed);
    plan_free(&p);
}

TEST(plan_each_field_change_is_one_updated_with_only_its_flag)
{
    Entry before = given_memory();
    Entry after = given_memory();

    after.body = (char *)"hello again";
    assert_one_field(&before, &after, (EventDraft){.body_changed = true});
    after = given_memory();
    after.key = (char *)"k:1";
    assert_one_field(&before, &after, (EventDraft){.key_changed = true});
    after = given_memory();
    after.tags = g_tags_abc;
    after.ntags = 3U;
    assert_one_field(&before, &after, (EventDraft){.tags_changed = true});
    after = given_memory();
    after.expires_at = (char *)"2030-01-01T00:00:00.000Z";
    assert_one_field(&before, &after, (EventDraft){.expires_changed = true});
}

TEST(plan_a_tag_replaced_by_another_of_the_same_count_is_a_tags_change)
{
    Entry before = given_memory();
    Entry after = given_memory();
    static char *swapped[] = {(char *)"alpha", (char *)"zeta"};

    after.tags = swapped;
    assert_one_field(&before, &after, (EventDraft){.tags_changed = true});
}

TEST(plan_a_key_or_expiry_cleared_is_a_change)
{
    Entry before = given_memory();
    Entry after = given_memory();

    before.key = (char *)"k:1";
    before.expires_at = (char *)"2030-01-01T00:00:00.000Z";
    assert_one_field(&before, &after, (EventDraft){.key_changed = true, .expires_changed = true});
}

TEST(plan_nothing_the_event_list_names_changed_is_no_event)
{
    Entry before = given_memory();
    Entry after = given_memory();
    Plan p;

    after.updated_at = (char *)"2026-11-18T11:00:00.000Z";
    after.updated_by = (char *)k_actor_agent;
    after.version_vector = (char *)"{\"01a0d90a-e71c-7a0a-bb2c-3b377482fa51\":2}";
    p = given_plan(&(EventRow){.before = &before, .after = &after});
    ASSERT_EQ_INT(p.n, 0);
    ASSERT_TRUE(p.drafts == NULL);
}

TEST(plan_a_link_that_came_and_one_that_went_are_one_updated_each)
{
    Entry before = given_memory();
    Entry after = given_memory();
    const EventLink gone = {.kind = "supersedes",
                            .direction = "incoming",
                            .other_id = "01a14521-09ff-7d20-8c00-00000000aa03",
                            .other_key = NULL};
    const EventLink kept = {.kind = "related",
                            .direction = "both",
                            .other_id = "01a14521-09ff-7d20-8c00-00000000aa04",
                            .other_key = NULL};
    const EventLink was[] = {kept, gone};
    const EventLink now_has[] = {kept, k_link_out};
    Plan p = given_plan(&(EventRow){.before = &before,
                                    .after = &after,
                                    .links_before = was,
                                    .links_after = now_has,
                                    .nlinks_before = 2U,
                                    .nlinks_after = 2U});

    ASSERT_EQ_INT(p.n, 2);
    if (p.n != 2) {
        return;
    }
    ASSERT_EQ_INT(p.drafts[0].type, EVENT_UPDATED);
    ASSERT_TRUE(p.drafts[0].has_link);
    ASSERT_STREQ(p.drafts[0].link.kind, "cites");
    ASSERT_STREQ(p.drafts[0].link.direction, "outgoing");
    ASSERT_EQ_INT(p.drafts[1].type, EVENT_UPDATED);
    ASSERT_TRUE(p.drafts[1].has_link);
    ASSERT_STREQ(p.drafts[1].link.kind, "supersedes");
    ASSERT_STREQ(p.drafts[1].link.direction, "incoming");
    plan_free(&p);
}

TEST(plan_the_same_other_memory_under_another_kind_or_direction_is_another_link)
{
    Entry before = given_memory();
    Entry after = given_memory();
    const EventLink was[] = {{.kind = "cites", .direction = "outgoing", .other_id = k_other_id}};
    const EventLink kind_changed[] = {
        {.kind = "supersedes", .direction = "outgoing", .other_id = k_other_id}};
    const EventLink dir_changed[] = {
        {.kind = "cites", .direction = "incoming", .other_id = k_other_id}};
    Plan p = given_plan(&(EventRow){.before = &before,
                                    .after = &after,
                                    .links_before = was,
                                    .links_after = kind_changed,
                                    .nlinks_before = 1U,
                                    .nlinks_after = 1U});

    ASSERT_EQ_INT(p.n, 2);
    if (p.n != 2) {
        return;
    }
    plan_free(&p);
    p = given_plan(&(EventRow){.before = &before,
                               .after = &after,
                               .links_before = was,
                               .links_after = dir_changed,
                               .nlinks_before = 1U,
                               .nlinks_after = 1U});
    ASSERT_EQ_INT(p.n, 2);
    if (p.n != 2) {
        return;
    }
    plan_free(&p);
}

TEST(plan_fields_and_a_link_are_separate_events_fields_first)
{
    Entry before = given_memory();
    Entry after = given_memory();
    Plan p;

    after.body = (char *)"changed";
    p = given_plan(&(EventRow){
        .before = &before, .after = &after, .links_after = &k_link_related, .nlinks_after = 1U});
    ASSERT_EQ_INT(p.n, 2);
    if (p.n != 2) {
        return;
    }
    ASSERT_FALSE(p.drafts[0].has_link);
    ASSERT_TRUE(p.drafts[0].body_changed);
    ASSERT_TRUE(p.drafts[1].has_link);
    ASSERT_FALSE(p.drafts[1].body_changed);
    plan_free(&p);
}

TEST(plan_a_link_of_a_new_or_removed_row_adds_no_event)
{
    Entry row = given_memory();
    Plan p = given_plan(&(EventRow){
        .before = NULL, .after = &row, .links_after = &k_link_related, .nlinks_after = 1U});

    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    plan_free(&p);
    p = given_plan(&(EventRow){
        .before = &row, .after = NULL, .links_before = &k_link_related, .nlinks_before = 1U});
    ASSERT_EQ_INT(p.n, 1);
    if (p.n != 1) {
        return;
    }
    plan_free(&p);
}

/* ---- the filter ------------------------------------------------------------------------------ */

#define ACTION(events, filter)                                                                     \
    "{\"name\":\"go\",\"events\":" events ",\"run\":{\"command\":[\"/bin/echo\"]}" filter "}"
#define FILTER(body) ",\"filter\":{" body "}"
#define ALL_TYPES "[\"memory.*\"]"
#define CREATED_ONLY "[\"memory.created\"]"

/* One plugin named hooker with the given actions (the text between the brackets). */
static void given_rules(EventRules *rules, const char *actions)
{
    char doc[8192];
    PluginManifest *man = NULL;
    int n = snprintf(doc, sizeof(doc),
                     "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"hooker\","
                     "\"plugin_version\":\"1.0.0\",\"protocol\":1,\"actions\":[%s]}",
                     actions);

    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(doc));
    ASSERT_EQ_INT(plugin_manifest_parse(doc, strlen(doc), &man), PLUGIN_PARSE_OK);
    memset(rules, 0, sizeof(*rules));
    ASSERT_EQ_INT(events_rules_add(rules, man), 0);
    plugin_manifest_free(man);
}

static const char *const k_tags_alpha_beta[] = {"alpha", "beta"};
static const char *const k_tags_todo[] = {"todo", "p:one"};

static EventFacts given_facts(const EventCause *cause)
{
    EventFacts f;

    memset(&f, 0, sizeof(f));
    f.tags = k_tags_alpha_beta;
    f.ntags = 2U;
    f.created_by_kind = "human";
    f.updated_by_kind = "agent";
    f.cause = cause;
    f.type = EVENT_CREATED;
    f.origin = STORE_ORIGIN_LOCAL;
    f.bin = STORE_BIN_LIVE;
    f.hooks = STORE_HOOKS_DEFAULT;
    return f;
}

static EventMatch match_of(const char *action, EventFacts facts)
{
    EventRules rules;
    EventMatch m = EVENT_SKIP_FILTER;

    given_rules(&rules, action);
    m = events_match(&rules.actions[0], &facts);
    events_rules_free(&rules);
    return m;
}

TEST(rules_hold_the_actions_and_not_the_filters_of_a_plugin)
{
    EventRules rules;
    PluginManifest *man = NULL;
    const char *doc =
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"hooker\","
        "\"plugin_version\":\"1.0.0\",\"protocol\":1,\"actions\":[" ACTION(
            CREATED_ONLY,
            "") ","
                "{\"name\":\"two\",\"events\":[\"memory.deleted\"],"
                "\"run\":{\"command\":[\"/bin/echo\"]}}],\"filters\":[{\"name\":\"rank\","
                "\"point\":\"search.results\",\"run\":{\"process\":\"persistent\","
                "\"command\":[\"hello.py\"]}}]}";

    ASSERT_EQ_INT(plugin_manifest_parse(doc, strlen(doc), &man), PLUGIN_PARSE_OK);
    memset(&rules, 0, sizeof(rules));
    ASSERT_EQ_INT(events_rules_add(&rules, man), 0);
    ASSERT_EQ_INT(rules.count, 2);
    ASSERT_STREQ(rules.actions[0].recipient, "plugin/hooker/go");
    ASSERT_STREQ(rules.actions[1].recipient, "plugin/hooker/two");
    ASSERT_STREQ(rules.actions[0].plugin, "hooker");
    ASSERT_EQ_INT(rules.actions[0].skipped_filter, 0);
    ASSERT_EQ_INT(events_rules_add(&rules, man), 0);
    ASSERT_EQ_INT(rules.count, 4);
    events_rules_free(&rules);
    ASSERT_EQ_INT(rules.count, 0);
    ASSERT_TRUE(rules.actions == NULL);
    plugin_manifest_free(man);
}

TEST(an_action_with_no_filter_matches_a_local_event_of_its_type_only)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    ASSERT_EQ_INT(match_of(ACTION(CREATED_ONLY, ""), f), EVENT_MATCH);
    f.type = EVENT_UPDATED;
    ASSERT_EQ_INT(match_of(ACTION(CREATED_ONLY, ""), f), EVENT_SKIP_FILTER);
    events_cause_free(&cause);
}

TEST(memory_star_lists_every_type_and_a_list_each_listed_one)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);
    int type = 0;

    for (type = 0; type < EVENT_TYPE_COUNT; type++) {
        f.type = (EventType)type;
        ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, ""), f), EVENT_MATCH);
        ASSERT_EQ_INT(match_of(ACTION("[\"memory.deleted\",\"memory.purged\"]", ""), f),
                      (type == (int)EVENT_DELETED || type == (int)EVENT_PURGED)
                          ? EVENT_MATCH
                          : EVENT_SKIP_FILTER);
    }
    events_cause_free(&cause);
}

TEST(tags_any_needs_one_pattern_to_match_a_tag)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"nope\",\"beta\"]}")), f),
                  EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"nope\",\"bet\"]}")), f),
                  EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"beta2\"]}")), f),
                  EVENT_SKIP_FILTER);
    events_cause_free(&cause);
}

TEST(tags_all_needs_every_pattern_to_match_some_tag)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"all\":[\"alpha\",\"beta\"]}")), f),
                  EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"all\":[\"alpha\",\"nope\"]}")), f),
                  EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"all\":[\"nope\",\"alpha\"]}")), f),
                  EVENT_SKIP_FILTER);
    events_cause_free(&cause);
}

TEST(any_and_all_must_both_pass_and_an_empty_list_is_no_test)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    ASSERT_EQ_INT(
        match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"alpha\"],\"all\":[\"nope\"]}")), f),
        EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(
        match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"nope\"],\"all\":[\"alpha\"]}")), f),
        EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[],\"all\":[]}")), f),
                  EVENT_MATCH);
    f.ntags = 0U;
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[]}")), f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"alpha\"]}")), f),
                  EVENT_SKIP_FILTER);
    events_cause_free(&cause);
}

TEST(a_tag_pattern_ending_in_star_is_a_prefix)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    f.tags = k_tags_todo;
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"p:*\"]}")), f),
                  EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"q:*\"]}")), f),
                  EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"all\":[\"to*\",\"p:o*\"]}")), f),
                  EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"all\":[\"to*\",\"x*\"]}")), f),
                  EVENT_SKIP_FILTER);
    /* a lone star matches every tag, but a memory with no tag has none to match */
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"*\"]}")), f),
                  EVENT_MATCH);
    /* a star inside is a plain character */
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"p*one\"]}")), f),
                  EVENT_SKIP_FILTER);
    f.ntags = 0U;
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"*\"]}")), f),
                  EVENT_SKIP_FILTER);
    events_cause_free(&cause);
}

TEST(match_before_lets_the_tags_before_the_write_pass_too)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);
    const char *any_todo = ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"todo\"]}"));
    const char *any_todo_before =
        ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"todo\"],\"match_before\":true}"));
    const char *any_todo_not_before =
        ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"todo\"],\"match_before\":false}"));

    f.type = EVENT_UPDATED;
    f.has_before = true;
    f.tags_before = k_tags_todo;
    f.ntags_before = 2U;
    ASSERT_EQ_INT(match_of(any_todo, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(any_todo_not_before, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(any_todo_before, f), EVENT_MATCH);
    f.has_before = false; /* a new memory has no before */
    ASSERT_EQ_INT(match_of(any_todo_before, f), EVENT_SKIP_FILTER);
    f.has_before = true;
    f.tags_before = k_tags_alpha_beta;
    ASSERT_EQ_INT(match_of(any_todo_before, f), EVENT_SKIP_FILTER);
    f.tags = k_tags_todo; /* the tags after pass */
    ASSERT_EQ_INT(match_of(any_todo_before, f), EVENT_MATCH);
    events_cause_free(&cause);
}

TEST(created_by_and_updated_by_are_tested_by_actor_kind)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"created_by\":[\"human\"]")), f),
                  EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"created_by\":[\"agent\",\"tool\"]")), f),
                  EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"updated_by\":[\"agent\"]")), f),
                  EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"updated_by\":[\"human\"]")), f),
                  EVENT_SKIP_FILTER);
    /* the two lists are separate */
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"created_by\":[\"agent\"]")), f),
                  EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"updated_by\":[]")), f), EVENT_MATCH);
    events_cause_free(&cause);
}

TEST(bins_tests_the_bin_the_memory_had_before_the_write)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);
    const char *live_only = ACTION(ALL_TYPES, FILTER("\"bins\":[\"live\"]"));
    const char *trash_or_expired = ACTION(ALL_TYPES, FILTER("\"bins\":[\"deleted\",\"expired\"]"));

    ASSERT_EQ_INT(match_of(live_only, f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(trash_or_expired, f), EVENT_SKIP_FILTER);
    f.bin = STORE_BIN_DELETED;
    ASSERT_EQ_INT(match_of(live_only, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(trash_or_expired, f), EVENT_MATCH);
    f.bin = STORE_BIN_EXPIRED;
    ASSERT_EQ_INT(match_of(trash_or_expired, f), EVENT_MATCH);
    events_cause_free(&cause);
}

/* A cause with a hook and a chain, built the way a run's write builds it. */
static EventCause given_cause_in_run(void)
{
    EventCause c = given_direct_cause();

    (void)snprintf(c.hook, sizeof(c.hook), "%s", "plugin/url_expand/fetch");
    c.depth = 2;
    c.nchain = 2U;
    (void)snprintf(c.chain_hooks[0], sizeof(c.chain_hooks[0]), "%s", "plugin/router/run");
    (void)snprintf(c.chain_hooks[1], sizeof(c.chain_hooks[1]), "%s", "plugin/url_expand/fetch");
    return c;
}

TEST(not_caused_by_skips_an_event_a_named_hook_or_plugin_caused)
{
    EventCause direct = given_direct_cause();
    EventCause run = given_cause_in_run();
    EventFacts f = given_facts(&run);
    const char *full = ACTION(ALL_TYPES, FILTER("\"not_caused_by\":[\"plugin/url_expand/fetch\"]"));
    const char *bare = ACTION(ALL_TYPES, FILTER("\"not_caused_by\":[\"url_expand\"]"));
    const char *chain_only = ACTION(ALL_TYPES, FILTER("\"not_caused_by\":[\"plugin/router/run\"]"));
    const char *other_entry =
        ACTION(ALL_TYPES, FILTER("\"not_caused_by\":[\"plugin/url_expand/other\"]"));
    const char *prefix_only = ACTION(ALL_TYPES, FILTER("\"not_caused_by\":[\"url\",\"route\"]"));
    const char *longer = ACTION(ALL_TYPES, FILTER("\"not_caused_by\":[\"url_expand_more\"]"));

    ASSERT_EQ_INT(match_of(full, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(bare, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(chain_only, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(other_entry, f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(prefix_only, f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(longer, f), EVENT_MATCH);
    /* the hook alone, with the chain empty, and the chain alone, with no hook */
    run.nchain = 0U;
    ASSERT_EQ_INT(match_of(full, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(chain_only, f), EVENT_MATCH);
    run.hook[0] = '\0';
    run.nchain = 1U;
    ASSERT_EQ_INT(match_of(chain_only, f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(full, f), EVENT_MATCH);
    /* a direct write passes every not_caused_by */
    f.cause = &direct;
    ASSERT_EQ_INT(match_of(full, f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(bare, f), EVENT_MATCH);
    events_cause_free(&direct);
    events_cause_free(&run);
}

TEST(origins_default_to_local_and_an_empty_list_matches_none)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);
    const char *dflt = ACTION(ALL_TYPES, "");
    const char *sync_too = ACTION(ALL_TYPES, FILTER("\"origins\":[\"local\",\"sync\"]"));
    const char *none = ACTION(ALL_TYPES, FILTER("\"origins\":[]"));
    const char *import_only = ACTION(ALL_TYPES, FILTER("\"origins\":[\"import\"]"));

    ASSERT_EQ_INT(match_of(dflt, f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(none, f), EVENT_SKIP_ORIGIN);
    ASSERT_EQ_INT(match_of(import_only, f), EVENT_SKIP_ORIGIN);
    f.origin = STORE_ORIGIN_SYNC;
    ASSERT_EQ_INT(match_of(dflt, f), EVENT_SKIP_ORIGIN);
    ASSERT_EQ_INT(match_of(sync_too, f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(import_only, f), EVENT_SKIP_ORIGIN);
    f.origin = STORE_ORIGIN_IMPORT;
    ASSERT_EQ_INT(match_of(dflt, f), EVENT_SKIP_ORIGIN);
    ASSERT_EQ_INT(match_of(sync_too, f), EVENT_SKIP_ORIGIN);
    ASSERT_EQ_INT(match_of(import_only, f), EVENT_MATCH);
    events_cause_free(&cause);
}

TEST(hooks_on_ignores_origins_but_not_the_rest_of_the_filter)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    f.origin = STORE_ORIGIN_IMPORT;
    f.hooks = STORE_HOOKS_ON;
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, ""), f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"origins\":[]")), f), EVENT_MATCH);
    ASSERT_EQ_INT(match_of(ACTION(CREATED_ONLY, FILTER("\"tags\":{\"any\":[\"nope\"]}")), f),
                  EVENT_SKIP_FILTER);
    f.hooks = STORE_HOOKS_DEFAULT;
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, ""), f), EVENT_SKIP_ORIGIN);
    events_cause_free(&cause);
}

TEST(a_miss_of_another_filter_is_a_filter_miss_even_when_origins_miss_too)
{
    EventCause cause = given_direct_cause();
    EventFacts f = given_facts(&cause);

    f.origin = STORE_ORIGIN_SYNC;
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"nope\"]}")), f),
                  EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION("[\"memory.deleted\"]", ""), f), EVENT_SKIP_FILTER);
    ASSERT_EQ_INT(match_of(ACTION(ALL_TYPES, ""), f), EVENT_SKIP_ORIGIN);
    events_cause_free(&cause);
}

TEST(a_miss_is_counted_on_the_action_by_its_kind)
{
    EventRules rules;
    EventAction *a = NULL;

    given_rules(&rules, ACTION(ALL_TYPES, ""));
    a = &rules.actions[0];
    events_count_skip(a, EVENT_MATCH, EVENT_CREATED);
    ASSERT_EQ_INT(a->skipped_filter, 0);
    events_count_skip(a, EVENT_SKIP_FILTER, EVENT_UPDATED);
    events_count_skip(a, EVENT_SKIP_FILTER, EVENT_UPDATED);
    ASSERT_EQ_INT(a->skipped_filter, 2);
    events_count_skip(a, EVENT_SKIP_ORIGIN, EVENT_PURGED);
    ASSERT_EQ_INT(a->skipped_filter, 2);
    ASSERT_EQ_INT(a->skipped_source[EVENT_PURGED], 1);
    ASSERT_EQ_INT(a->skipped_source[EVENT_UPDATED], 0);
    events_rules_free(&rules);
}

/* ---- the payload -------------------------------------------------------------------------------
 */

static EventStamp given_stamp(const EventCause *cause)
{
    return (EventStamp){.event_id = k_event_id,
                        .batch_id = k_batch_id,
                        .at = k_at,
                        .brain_path = "~/b.db",
                        .device_id = k_device,
                        .command = "add",
                        .cause = cause,
                        .origin = STORE_ORIGIN_LOCAL};
}

typedef struct {
    EventDraft draft;
    EventRow row;
    const Entry *memory;
    EventLinks links;
} PayloadCase;

static char *given_payload(const EventCause *cause, PayloadCase pcase)
{
    EventStamp st = given_stamp(cause);
    EventMemory memory = {.entry = pcase.memory,
                          .links = pcase.links.rows,
                          .nlinks = pcase.links.count,
                          .change_seq = 118};
    char *text = events_payload(&st, &pcase.draft, &pcase.row, &memory);

    ASSERT_TRUE(text != NULL);
    return text != NULL ? text : (char *)calloc(1U, 1U);
}

static const char k_memory_json[] =
    "{\"id\":\"01a14521-09ff-7d20-8c00-00000000aa01\",\"key\":null,\"body\":\"hello\","
    "\"tags\":[\"alpha\",\"beta\"],\"created_by\":{\"kind\":\"human\",\"id\":null},"
    "\"updated_by\":{\"kind\":\"human\",\"id\":null},\"created_at\":\"2026-11-18T10:00:00.000Z\","
    "\"updated_at\":\"2026-11-18T10:00:00.000Z\",\"expires_at\":null,\"deleted_at\":null,"
    "\"bin\":\"live\",\"change_seq\":118,\"links\":[]}";

#define PAYLOAD_HEAD(type)                                                                         \
    "{\"event_id\":\"01a14521-0a00-7c11-9a00-0000000000e1\",\"event_type\":\"" type                \
    "\",\"occurred_at\":\"2026-11-18T10:00:00.000Z\",\"detected_at\":\"2026-11-18T10:00:00."       \
    "000Z\","                                                                                      \
    "\"batch_id\":\"01a14521-0b00-7c11-9a00-0000000000b1\","                                       \
    "\"cause\":{\"command\":\"add\",\"origin\":\"local\",\"hook\":null,\"run_id\":null,"           \
    "\"depth\":0,\"chain\":[]},\"brain\":{\"path\":\"~/b.db\",\"device_id\":"                      \
    "\"01a0d90a-e71c-7a0a-bb2c-3b377482fa51\"},\"memory\":"
#define PAYLOAD_TAIL                                                                               \
    ",\"truncated\":false,\"truncated_fields\":[],\"limits\":{\"max_bytes\":262144}}"

TEST(a_created_payload_is_the_frozen_h3_data_without_the_recipient_fields)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    char *text = given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_CREATED},
                                                     .row = {.after = &after},
                                                     .memory = &after});
    char want[2048];

    (void)snprintf(want, sizeof(want),
                   PAYLOAD_HEAD("memory.created") "%s,\"changes\":null" PAYLOAD_TAIL,
                   k_memory_json);
    ASSERT_STREQ(text, want);
    ASSERT_STR_NOT_CONTAINS(text, "\"plugin\":");
    ASSERT_STR_NOT_CONTAINS(text, "\"entry\":");
    ASSERT_STR_NOT_CONTAINS(text, "data_dir");
    ASSERT_STR_NOT_CONTAINS(text, "\"settings\":");
    ASSERT_STR_NOT_CONTAINS(text, "local_id");
    ASSERT_STR_NOT_CONTAINS(text, "version_vector");
    free(text);
    events_cause_free(&cause);
}

TEST(deleted_restored_and_purged_payloads_carry_null_changes)
{
    EventCause cause = given_direct_cause();
    Entry memory = given_memory();
    int type = 0;

    for (type = EVENT_DELETED; type <= EVENT_PURGED; type++) {
        char *text = given_payload(&cause, (PayloadCase){.draft = {.type = (EventType)type},
                                                         .row = {.before = &memory},
                                                         .memory = &memory});

        ASSERT_STR_CONTAINS(text, event_type_name((EventType)type));
        ASSERT_STR_CONTAINS(text, ",\"changes\":null,\"truncated\":false");
        free(text);
    }
    events_cause_free(&cause);
}

TEST(an_updated_payload_names_the_changed_fields_and_their_old_values)
{
    EventCause cause = given_direct_cause();
    Entry before = given_memory();
    Entry after = given_memory();
    char *text = NULL;

    before.tags = g_tags_abc;
    before.ntags = 3U;
    after.key = (char *)"k:1";
    after.body = (char *)"hello again";
    after.expires_at = (char *)"2030-01-01T00:00:00.000Z";
    text = given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_UPDATED,
                                                         .body_changed = true,
                                                         .key_changed = true,
                                                         .tags_changed = true,
                                                         .expires_changed = true},
                                               .row = {.before = &before, .after = &after},
                                               .memory = &after});
    ASSERT_STR_CONTAINS(text,
                        ",\"changes\":{\"fields\":[\"key\",\"body\",\"tags\",\"expires_at\"],"
                        "\"before\":{\"key\":null,\"body\":\"hello\","
                        "\"tags\":[\"alpha\",\"beta\",\"gamma\"],\"expires_at\":null},"
                        "\"tags_added\":[],\"tags_removed\":[\"gamma\"]},\"truncated\":false");
    ASSERT_STR_CONTAINS(text, "\"key\":\"k:1\",\"body\":\"hello again\"");
    free(text);
    events_cause_free(&cause);
}

TEST(an_updated_payload_for_one_field_lists_only_that_field)
{
    EventCause cause = given_direct_cause();
    Entry before = given_memory();
    Entry after = given_memory();
    char *text = NULL;
    static char *added[] = {(char *)"alpha", (char *)"beta", (char *)"delta"};

    after.tags = added;
    after.ntags = 3U;
    text =
        given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_UPDATED, .tags_changed = true},
                                            .row = {.before = &before, .after = &after},
                                            .memory = &after});
    ASSERT_STR_CONTAINS(text, ",\"changes\":{\"fields\":[\"tags\"],\"before\":{\"tags\":[\"alpha\","
                              "\"beta\"]},\"tags_added\":[\"delta\"],\"tags_removed\":[]},");
    free(text);
    after = given_memory();
    after.key = (char *)"k:2";
    before.key = (char *)"k:1";
    text =
        given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_UPDATED, .key_changed = true},
                                            .row = {.before = &before, .after = &after},
                                            .memory = &after});
    ASSERT_STR_CONTAINS(text, ",\"changes\":{\"fields\":[\"key\"],\"before\":{\"key\":\"k:1\"},"
                              "\"tags_added\":[],\"tags_removed\":[]},");
    free(text);
    events_cause_free(&cause);
}

TEST(a_link_payload_carries_changes_link_and_the_link_stubs_have_no_local_id)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    StoreNeighbor stub;
    char *text = NULL;

    memset(&stub, 0, sizeof(stub));
    stub.subject_id = 7;
    stub.from_id = 7;
    stub.to_id = 8;
    stub.neighbor_id = 8;
    stub.neighbor_key = (char *)"other:key";
    stub.neighbor_body = (char *)"other body";
    stub.neighbor_sync_id = (char *)k_other_id;
    stub.kind = STORE_EDGE_CITES;
    text = given_payload(
        &cause,
        (PayloadCase){.draft = {.type = EVENT_UPDATED, .has_link = true, .link = k_link_related},
                      .row = {.before = &after, .after = &after},
                      .memory = &after,
                      .links = {.rows = &stub, .count = 1U}});
    ASSERT_STR_CONTAINS(text, ",\"changes\":{\"link\":{\"kind\":\"related\",\"direction\":\"both\","
                              "\"other\":{\"id\":\"01a14521-09ff-7d20-8c00-00000000aa02\","
                              "\"key\":\"other:key\"}}},\"truncated\":false");
    ASSERT_STR_CONTAINS(text, "\"links\":[{\"id\":\"01a14521-09ff-7d20-8c00-00000000aa02\","
                              "\"key\":\"other:key\",\"type\":\"cites\",\"bin\":\"live\","
                              "\"preview\":\"other body\"}]}");
    free(text);
    text = given_payload(
        &cause,
        (PayloadCase){.draft = {.type = EVENT_UPDATED, .has_link = true, .link = k_link_out},
                      .row = {.before = &after, .after = &after},
                      .memory = &after});
    ASSERT_STR_CONTAINS(text, "\"direction\":\"outgoing\",\"other\":{\"id\":\"01a14521-09ff-7d20-"
                              "8c00-00000000aa02\",\"key\":null}}}");
    free(text);
    events_cause_free(&cause);
}

TEST(a_payload_written_in_a_run_names_the_hook_run_depth_and_chain)
{
    EventCause cause = given_cause_in_run();
    Entry after = given_memory();
    char *text = NULL;

    (void)snprintf(cause.run_id, sizeof(cause.run_id), "%s",
                   "01a14521-0bf0-7000-8000-0000000000c1");
    free(cause.chain_json);
    cause.chain_json = (char *)malloc(8U);
    (void)snprintf(cause.chain_json, 8U, "%s", "[1,2,3]");
    text = given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_CREATED},
                                               .row = {.after = &after},
                                               .memory = &after});
    ASSERT_STR_CONTAINS(text, "\"cause\":{\"command\":\"add\",\"origin\":\"local\",\"hook\":"
                              "\"plugin/url_expand/fetch\",\"run_id\":\"01a14521-0bf0-7000-8000-"
                              "0000000000c1\",\"depth\":2,\"chain\":[1,2,3]}");
    free(text);
    events_cause_free(&cause);
}

TEST(the_origin_and_command_of_the_stamp_are_written)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    EventStamp st = given_stamp(&cause);
    EventDraft draft = {.type = EVENT_CREATED};
    EventRow row = {.after = &after};
    char *text = NULL;

    st.origin = STORE_ORIGIN_IMPORT;
    st.command = "import";
    text = events_payload(
        &st, &draft, &row,
        &(EventMemory){.entry = &after, .links = NULL, .nlinks = 0U, .change_seq = 1});
    ASSERT_TRUE(text != NULL);
    ASSERT_STR_CONTAINS(text, "\"cause\":{\"command\":\"import\",\"origin\":\"import\"");
    free(text);
    events_cause_free(&cause);
}

/* How many bytes of chars follow the first marker in text; 0 when the marker is absent. */
static size_t run_after(const char *text, const char *marker, const char *chars)
{
    const char *at = strstr(text, marker);

    return at != NULL ? strspn(at + strlen(marker), chars) : 0U;
}

/* A body of n bytes of one character. */
static char *given_body(size_t n, char fill)
{
    char *body = (char *)malloc(n + 1U);

    ASSERT_TRUE(body != NULL);
    if (body == NULL) {
        return (char *)calloc(1U, 1U);
    }
    memset(body, fill, n);
    body[n] = '\0';
    return body;
}

/* `count` link stubs of about 125 bytes each. */
static StoreNeighbor *given_stubs(size_t count)
{
    StoreNeighbor *rows = (StoreNeighbor *)calloc(count, sizeof(*rows));
    size_t i = 0;

    ASSERT_TRUE(rows != NULL);
    for (i = 0; rows != NULL && i < count; i++) {
        rows[i].subject_id = 7;
        rows[i].from_id = 7;
        rows[i].to_id = 8;
        rows[i].neighbor_id = 8;
        rows[i].neighbor_key = (char *)"other:key";
        rows[i].neighbor_body = (char *)"other body";
        rows[i].neighbor_sync_id = (char *)k_other_id;
    }
    return rows;
}

TEST(a_payload_under_the_limit_is_not_cut)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    Entry before = given_memory();
    char *text = NULL;

    after.body = given_body(100000U, 'a');
    before.body = given_body(100000U, 'b');
    before.key = (char *)"k:1";
    text =
        given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_UPDATED, .body_changed = true},
                                            .row = {.before = &before, .after = &after},
                                            .memory = &after});
    ASSERT_TRUE(strlen(text) < (size_t)EVENT_PAYLOAD_MAX);
    ASSERT_STR_CONTAINS(text, ",\"truncated\":false,\"truncated_fields\":[],");
    ASSERT_STR_CONTAINS(text, "\"before\":{\"body\":\"bbbb");
    free(text);
    free(after.body);
    free(before.body);
    events_cause_free(&cause);
}

TEST(over_the_limit_the_old_body_goes_first_and_nothing_else_when_that_is_enough)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    Entry before = given_memory();
    StoreNeighbor *stubs = given_stubs(3U);
    char *text = NULL;

    after.body = given_body(150000U, 'a');
    before.body = given_body(150000U, 'b');
    text =
        given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_UPDATED, .body_changed = true},
                                            .row = {.before = &before, .after = &after},
                                            .memory = &after,
                                            .links = {.rows = stubs, .count = 3U}});
    ASSERT_TRUE(strlen(text) <= (size_t)EVENT_PAYLOAD_MAX);
    ASSERT_STR_CONTAINS(text, "{\"fields\":[\"body\"],\"before\":{},");
    ASSERT_STR_CONTAINS(text,
                        ",\"truncated\":true,\"truncated_fields\":[\"changes.before.body\"],");
    ASSERT_STR_CONTAINS(text, "\"key\":\"other:key\"");
    ASSERT_TRUE(strstr(text, "\"body\":\"aaaaaaaa") != NULL);
    free(text);
    free(after.body);
    free(before.body);
    free(stubs);
    events_cause_free(&cause);
}

/* The old body alone is over the limit and the new one is short: the old body is still cut. */
TEST(a_big_old_body_is_cut_when_the_new_body_is_short)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    Entry before = given_memory();
    char *text = NULL;

    after.body = given_body(100U, 'a');
    before.body = given_body(300000U, 'b');
    text =
        given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_UPDATED, .body_changed = true},
                                            .row = {.before = &before, .after = &after},
                                            .memory = &after});
    ASSERT_TRUE(strlen(text) <= (size_t)EVENT_PAYLOAD_MAX);
    ASSERT_STR_CONTAINS(text, "\"truncated_fields\":[\"changes.before.body\"]");
    free(text);
    free(after.body);
    free(before.body);
    events_cause_free(&cause);
}

TEST(the_old_body_stays_in_a_changes_that_has_other_fields_and_loses_only_the_body)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    Entry before = given_memory();
    char *text = NULL;

    after.body = given_body(150000U, 'a');
    before.body = given_body(150000U, 'b');
    before.key = (char *)"k:1";
    text = given_payload(
        &cause,
        (PayloadCase){.draft = {.type = EVENT_UPDATED, .body_changed = true, .key_changed = true},
                      .row = {.before = &before, .after = &after},
                      .memory = &after});
    ASSERT_STR_CONTAINS(text, "{\"fields\":[\"key\",\"body\"],\"before\":{\"key\":\"k:1\"},");
    ASSERT_STR_CONTAINS(text, "\"truncated_fields\":[\"changes.before.body\"]");
    free(text);
    free(after.body);
    free(before.body);
    events_cause_free(&cause);
}

TEST(over_the_limit_with_no_old_body_the_links_go_next)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    StoreNeighbor *stubs = given_stubs(3000U);
    char *text = NULL;

    after.body = given_body(60000U, 'a');
    text = given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_CREATED},
                                               .row = {.after = &after},
                                               .memory = &after,
                                               .links = {.rows = stubs, .count = 3000U}});
    ASSERT_TRUE(strlen(text) <= (size_t)EVENT_PAYLOAD_MAX);
    ASSERT_STR_CONTAINS(text, ",\"truncated\":true,\"truncated_fields\":[\"memory.links\"],");
    ASSERT_STR_CONTAINS(text, ",\"links\":[]}");
    ASSERT_TRUE(strstr(text, "\"body\":\"aaaaaaaa") != NULL);
    ASSERT_TRUE(strstr(text, "\"body\":\"aaaaaaaa\",\"tags\"") == NULL); /* the body is whole */
    free(text);
    free(after.body);
    free(stubs);
    events_cause_free(&cause);
}

TEST(truncation_order_256k)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    Entry before = given_memory();
    StoreNeighbor *stubs = given_stubs(3000U);
    char *text = NULL;

    after.body = given_body(300000U, 'a');
    before.body = given_body(300000U, 'b');
    text =
        given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_UPDATED, .body_changed = true},
                                            .row = {.before = &before, .after = &after},
                                            .memory = &after,
                                            .links = {.rows = stubs, .count = 3000U}});
    ASSERT_TRUE(strlen(text) <= (size_t)EVENT_PAYLOAD_MAX);
    ASSERT_STR_CONTAINS(text, ",\"truncated\":true,\"truncated_fields\":[\"changes.before.body\","
                              "\"memory.links\",\"memory.body\"],");
    ASSERT_STR_CONTAINS(text, ",\"links\":[]}");
    ASSERT_STR_CONTAINS(text, ",\"before\":{},");
    ASSERT_EQ_INT(run_after(text, "\"body\":\"", "a"), EVENT_BODY_CUT);
    free(text);
    free(after.body);
    free(before.body);
    free(stubs);
    events_cause_free(&cause);
}

TEST(the_body_is_cut_on_a_utf8_boundary)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    char *text = NULL;
    size_t i = 0;
    const size_t chars = 100000U;

    after.body = (char *)malloc((chars * 3U) + 1U);
    ASSERT_TRUE(after.body != NULL);
    if (after.body == NULL) {
        return;
    }
    for (i = 0; i < chars; i++) {
        memcpy(after.body + (i * 3U), "\xE2\x82\xAC", 3U); /* the euro sign, 3 bytes */
    }
    after.body[chars * 3U] = '\0';
    text = given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_CREATED},
                                               .row = {.after = &after},
                                               .memory = &after});
    ASSERT_TRUE(strlen(text) > 0U);
    ASSERT_STR_CONTAINS(text, "\"truncated_fields\":[\"memory.body\"]");
    /* 16384 = 5461 * 3 + 1: the cut lands inside the 5462nd sign, so 5461 stay */
    ASSERT_EQ_INT(run_after(text, "\"body\":\"", "\xE2\x82\xAC"), 5461U * 3U);
    free(text);
    free(after.body);
    events_cause_free(&cause);
}

TEST(a_body_of_exactly_the_cut_length_is_not_cut_even_in_a_big_payload)
{
    EventCause cause = given_direct_cause();
    Entry after = given_memory();
    StoreNeighbor *stubs = given_stubs(3000U);
    char *text = NULL;

    after.body = given_body((size_t)EVENT_BODY_CUT, 'a');
    text = given_payload(&cause, (PayloadCase){.draft = {.type = EVENT_CREATED},
                                               .row = {.after = &after},
                                               .memory = &after,
                                               .links = {.rows = stubs, .count = 3000U}});
    ASSERT_STR_CONTAINS(text, "\"truncated_fields\":[\"memory.links\"]");
    ASSERT_EQ_INT(strspn(strstr(text, "\"body\":\"") + 8, "a"), EVENT_BODY_CUT);
    free(text);
    free(after.body);
    free(stubs);
    events_cause_free(&cause);
}

TEST(recipients_json_lists_each_entry_pending)
{
    const char *const two[] = {"plugin/a/b", "plugin/c/d"};
    char *text = events_recipients_json(two, 2U);

    ASSERT_TRUE(text != NULL);
    ASSERT_STREQ(text, "[{\"entry\":\"plugin/a/b\",\"state\":\"pending\"},"
                       "{\"entry\":\"plugin/c/d\",\"state\":\"pending\"}]");
    free(text);
    text = events_recipients_json(two, 1U);
    ASSERT_STREQ(text, "[{\"entry\":\"plugin/a/b\",\"state\":\"pending\"}]");
    free(text);
    text = events_recipients_json(two, 0U);
    ASSERT_STREQ(text, "[]");
    free(text);
}

TEST(the_processes_switch_defaults_on_and_follows_the_setter)
{
    ASSERT_TRUE(events_processes());
    events_set_processes(false);
    ASSERT_FALSE(events_processes());
    events_set_processes(true);
    ASSERT_TRUE(events_processes());
}

/* ---- the cause from the environment -----------------------------------------------------------
 */

static const char *const k_cause_env[] = {
    "REMEMBER_HOOK_DEPTH", "REMEMBER_HOOK_NAME",      "REMEMBER_HOOK_RUN_ID",
    "REMEMBER_HOOK_CHAIN", "REMEMBER_CAUSE_EVENT_ID", "REMEMBER_ID",
    "REMEMBER_CAUSE_HOOK", "REMEMBER_CAUSE_RUN_ID",
};

/* Single-threaded tests. setenv and unsetenv have no portable reentrant form. */
static void clear_cause_env(void)
{
    size_t i = 0;

    for (i = 0; i < sizeof(k_cause_env) / sizeof(k_cause_env[0]); i++) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        ASSERT_EQ_INT(unsetenv(k_cause_env[i]), 0);
    }
}

static void put_env(const char *name, const char *value)
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(setenv(name, value, 1), 0);
}

static const char k_run_id[] = "01a14521-0bf0-7000-8000-0000000000c1";
static const char k_event2[] = "01a14521-0a00-7c11-9a00-0000000000e2";
static const char k_hop1[] =
    "{\"hook\":\"plugin/router/run\",\"event_id\":\"01a14521-0a00-7c11-9a00-0000000000e1\","
    "\"memory_id\":\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":1}";

/* A run at depth 2 of plugin/url_expand/fetch, handling event e2 of memory aa01. */
static void given_run_env(const char *chain)
{
    clear_cause_env();
    put_env("REMEMBER_HOOK_DEPTH", "2");
    put_env("REMEMBER_HOOK_NAME", "plugin/url_expand/fetch");
    put_env("REMEMBER_HOOK_RUN_ID", k_run_id);
    put_env("REMEMBER_CAUSE_EVENT_ID", k_event2);
    put_env("REMEMBER_ID", k_memory_id);
    if (chain != NULL) {
        put_env("REMEMBER_HOOK_CHAIN", chain);
    }
}

static void assert_no_chain(const EventCause *c)
{
    ASSERT_STREQ(c->chain_json, "[]");
    ASSERT_EQ_INT(c->nchain, 0);
}

TEST(a_write_outside_a_run_is_a_direct_cause)
{
    EventCause c;

    clear_cause_env();
    put_env("REMEMBER_HOOK_NAME", "plugin/url_expand/fetch"); /* no depth: not in a run */
    put_env("REMEMBER_HOOK_RUN_ID", k_run_id);
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_STREQ(c.hook, "");
    ASSERT_STREQ(c.run_id, "");
    ASSERT_EQ_INT(c.depth, 0);
    assert_no_chain(&c);
    events_cause_free(&c);
    ASSERT_TRUE(c.chain_json == NULL);
    clear_cause_env();
}

TEST(a_run_with_no_chain_variable_has_the_hook_run_and_depth_and_an_empty_chain)
{
    EventCause c;

    given_run_env(NULL);
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_STREQ(c.hook, "plugin/url_expand/fetch");
    ASSERT_STREQ(c.run_id, k_run_id);
    ASSERT_EQ_INT(c.depth, 2);
    assert_no_chain(&c);
    events_cause_free(&c);
    clear_cause_env();
}

TEST(a_valid_chain_gets_this_runs_hop_appended)
{
    EventCause c;
    char chain[512];

    (void)snprintf(chain, sizeof(chain), "[%s]", k_hop1);
    given_run_env(chain);
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_STREQ(c.chain_json, "[{\"hook\":\"plugin/router/run\",\"event_id\":"
                               "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":"
                               "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":1},"
                               "{\"hook\":\"plugin/url_expand/fetch\",\"event_id\":"
                               "\"01a14521-0a00-7c11-9a00-0000000000e2\",\"memory_id\":"
                               "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":2}]");
    ASSERT_EQ_INT(c.nchain, 2);
    ASSERT_STREQ(c.chain_hooks[0], "plugin/router/run");
    ASSERT_STREQ(c.chain_hooks[1], "plugin/url_expand/fetch");
    events_cause_free(&c);
    clear_cause_env();
}

TEST(an_empty_chain_variable_gives_a_chain_of_just_this_hop)
{
    EventCause c;

    given_run_env("[]");
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_EQ_INT(c.nchain, 1);
    ASSERT_STREQ(c.chain_hooks[0], "plugin/url_expand/fetch");
    ASSERT_STR_PREFIX(c.chain_json, "[{\"hook\":\"plugin/url_expand/fetch\",\"event_id\":");
    events_cause_free(&c);
    clear_cause_env();
}

/* Each bad piece leaves the chain empty and the rest of the cause as it was. */
static void assert_chain_dropped(const char *chain)
{
    EventCause c;

    given_run_env(chain);
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    assert_no_chain(&c);
    ASSERT_STREQ(c.hook, "plugin/url_expand/fetch");
    ASSERT_EQ_INT(c.depth, 2);
    events_cause_free(&c);
    clear_cause_env();
}

TEST(a_chain_that_is_not_valid_is_dropped_and_the_write_counts_as_depth_one_of_its_own)
{
    char many[16384];
    size_t len = 1U;
    int i = 0;

    assert_chain_dropped(NULL);
    assert_chain_dropped("");
    assert_chain_dropped("not json");
    assert_chain_dropped("{}");
    assert_chain_dropped("[1]");
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\"}]");
    /* a fifth member, a bad hook, a bad id, a bad depth, a text depth, a zero depth */
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\",\"event_id\":"
                         "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":"
                         "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":1,\"x\":1}]");
    assert_chain_dropped("[{\"hook\":\"router\",\"event_id\":"
                         "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":"
                         "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":1}]");
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\",\"event_id\":\"nope\",\"memory_id\":"
                         "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":1}]");
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\",\"event_id\":"
                         "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":\"nope\","
                         "\"depth\":1}]");
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\",\"event_id\":"
                         "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":"
                         "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":\"1\"}]");
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\",\"event_id\":"
                         "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":"
                         "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":0}]");
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\",\"event_id\":"
                         "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":"
                         "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":1.5}]");
    assert_chain_dropped("[{\"hook\":\"plugin/router/run\",\"event_id\":"
                         "\"01a14521-0a00-7c11-9a00-0000000000e1\",\"memory_id\":"
                         "\"01a14521-09ff-7d20-8c00-00000000aa01\",\"depth\":1000001}]");
    /* 100 items already: no room for this run's hop */
    many[0] = '[';
    for (i = 0; i < EVENT_CHAIN_MAX; i++) {
        len += (size_t)snprintf(many + len, sizeof(many) - len, "%s%s", i > 0 ? "," : "", k_hop1);
    }
    many[len++] = ']';
    many[len] = '\0';
    assert_chain_dropped(many);
}

TEST(ninety_nine_hops_and_this_one_fill_the_chain)
{
    EventCause c;
    char many[16384];
    size_t len = 1U;
    int i = 0;

    many[0] = '[';
    for (i = 0; i < EVENT_CHAIN_MAX - 1; i++) {
        len += (size_t)snprintf(many + len, sizeof(many) - len, "%s%s", i > 0 ? "," : "", k_hop1);
    }
    many[len++] = ']';
    many[len] = '\0';
    given_run_env(many);
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_EQ_INT(c.nchain, EVENT_CHAIN_MAX);
    ASSERT_STREQ(c.chain_hooks[EVENT_CHAIN_MAX - 1], "plugin/url_expand/fetch");
    events_cause_free(&c);
    clear_cause_env();
}

TEST(a_missing_or_bad_hop_variable_drops_the_chain)
{
    EventCause c;
    char chain[512];

    (void)snprintf(chain, sizeof(chain), "[%s]", k_hop1);
    given_run_env(chain);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(unsetenv("REMEMBER_CAUSE_EVENT_ID"), 0);
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    assert_no_chain(&c);
    events_cause_free(&c);
    given_run_env(chain);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(unsetenv("REMEMBER_ID"), 0);
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    assert_no_chain(&c);
    events_cause_free(&c);
    given_run_env(chain);
    put_env("REMEMBER_ID", "not-a-uuid");
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    assert_no_chain(&c);
    events_cause_free(&c);
    given_run_env(chain);
    put_env("REMEMBER_CAUSE_EVENT_ID", "01A14521-0A00-7C11-9A00-0000000000E2"); /* not lowercase */
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    assert_no_chain(&c);
    events_cause_free(&c);
    clear_cause_env();
}

TEST(a_bad_hook_name_or_run_id_reads_as_none_and_the_depth_stays)
{
    EventCause c;
    static const char *const bad_names[] = {"plugin/",       "plugin//run",    "plugin/Upper/run",
                                            "plugin/a/b/c",  "plugin/a/",      "other/a/b",
                                            "plugin/1a/run", "plugin/a-b/run", ""};
    size_t i = 0;

    for (i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); i++) {
        given_run_env("[]");
        put_env("REMEMBER_HOOK_NAME", bad_names[i]);
        ASSERT_EQ_INT(events_cause_read(&c), 0);
        ASSERT_STREQ(c.hook, "");
        ASSERT_EQ_INT(c.depth, 2);
        assert_no_chain(&c);
        events_cause_free(&c);
    }
    given_run_env("[]");
    put_env("REMEMBER_HOOK_RUN_ID", "not-a-uuid");
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_STREQ(c.run_id, "");
    ASSERT_STREQ(c.hook, "plugin/url_expand/fetch");
    events_cause_free(&c);
    given_run_env("[]");
    put_env("REMEMBER_HOOK_NAME", "plugin/persistent_one"); /* a persistent process has no entry */
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_STREQ(c.hook, "plugin/persistent_one");
    events_cause_free(&c);
    clear_cause_env();
}

TEST(a_depth_that_is_not_a_whole_number_of_one_to_a_million_is_not_a_run)
{
    static const char *const bad[] = {"0",  "-1", "abc", "1x",      "",
                                      " 1", "+1", "1.5", "1000001", "99999999999999999999"};
    EventCause c;
    size_t i = 0;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        given_run_env("[]");
        put_env("REMEMBER_HOOK_DEPTH", bad[i]);
        ASSERT_EQ_INT(events_cause_read(&c), 0);
        ASSERT_EQ_INT(c.depth, 0);
        ASSERT_STREQ(c.hook, "");
        events_cause_free(&c);
    }
    given_run_env("[]");
    put_env("REMEMBER_HOOK_DEPTH", "1000000");
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_EQ_INT(c.depth, 1000000);
    events_cause_free(&c);
    given_run_env("[]");
    put_env("REMEMBER_HOOK_DEPTH", "1");
    ASSERT_EQ_INT(events_cause_read(&c), 0);
    ASSERT_EQ_INT(c.depth, 1);
    events_cause_free(&c);
    clear_cause_env();
}

/* A link alone (no field changed): the first draft is the link's, and when it cannot be kept the
   plan is empty. */
TEST(planning_a_link_fails_cleanly_when_memory_runs_out)
{
    Entry before = given_memory();
    Entry after = given_memory();
    const EventLink now_has[] = {k_link_related};
    EventDraft *drafts = NULL;
    size_t n = 7U;

    test_alloc_fail_after(0);
    ASSERT_FALSE(events_plan(
        &(EventRow){.before = &before, .after = &after, .links_after = now_has, .nlinks_after = 1U},
        &drafts, &n));
    test_alloc_fail_after(-1);
    ASSERT_EQ_INT(n, 0);
    ASSERT_TRUE(drafts == NULL);
}

/* events_rules_load: a plugin key the store does not know is an internal error, an enabled
   plugin gives its action, and a failed allocation is out of memory. */
/* More drafts than the first block holds: the list grows and keeps every draft. */
TEST(planning_ten_new_links_keeps_ten_drafts)
{
    Entry before = given_memory();
    Entry after = given_memory();
    EventLink now_has[10];
    static const char *const kinds[] = {"k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9"};
    size_t i = 0;
    Plan p;

    for (i = 0; i < 10U; i++) {
        now_has[i] = k_link_out;
        now_has[i].kind = kinds[i];
    }
    p = given_plan(&(EventRow){
        .before = &before, .after = &after, .links_after = now_has, .nlinks_after = 10U});
    ASSERT_EQ_INT(p.n, 10);
    ASSERT_STREQ(p.n == 10U ? p.drafts[9].link.kind : "", "k9");
    plan_free(&p);
}

TEST(loading_the_rules_reads_enabled_and_reports_why_it_failed)
{
    static const char k_doc[] =
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"hooker\","
        "\"plugin_version\":\"1.0.0\",\"protocol\":1,\"actions\":[" ACTION(ALL_TYPES, "") "]}";
    char err[ERR_BUFSIZE];
    char *db = make_temp_db_path();
    Store *s = store_open(db, err, sizeof(err));
    SettingsRegistry *reg = settings_registry_new();
    PluginManifest *man = NULL;
    PluginManifestList mans;
    EventRules rules;
    SettingWrite w;
    SettingWriteResult wr;
    const char *on = "true";

    ASSERT_TRUE(s != NULL && reg != NULL);
    ASSERT_EQ_INT(plugin_manifest_parse(k_doc, strlen(k_doc), &man), PLUGIN_PARSE_OK);
    mans = (PluginManifestList){.items = &man, .count = 1U};
    memset(&rules, 0, sizeof(rules));
    ASSERT_EQ_INT(events_rules_load(s, NULL, &mans, &rules, err, sizeof(err)), -1);
    ASSERT_STREQ(err, "internal error");
    events_rules_free(&rules);

    ASSERT_EQ_STATUS(plugin_settings_register(man, reg), PLUGIN_SETTINGS_OK);
    store_set_settings_registry(s, reg);
    memset(&w, 0, sizeof(w));
    w.key = "plugin.hooker.enabled";
    w.items = &on;
    w.nitems = 1U;
    w.layer = SETTING_LAYER_BRAIN;
    ASSERT_EQ_INT(store_setting_set(s, NULL, &w, &wr, err, sizeof(err)), SETTING_OK);
    memset(&rules, 0, sizeof(rules));
    ASSERT_EQ_INT(events_rules_load(s, NULL, &mans, &rules, err, sizeof(err)), 0);
    ASSERT_EQ_INT(rules.count, 1);
    events_rules_free(&rules);

    memset(&rules, 0, sizeof(rules));
    test_alloc_fail_after(0);
    ASSERT_EQ_INT(events_rules_load(s, NULL, &mans, &rules, err, sizeof(err)), -1);
    test_alloc_fail_after(-1);
    ASSERT_STREQ(err, "out of memory");
    events_rules_free(&rules);
    store_close(s);
    settings_registry_free(reg);
    plugin_manifest_free(man);
    free(db);
}

TEST(reading_the_cause_fails_when_memory_runs_out)
{
    EventCause c;

    clear_cause_env();
    test_alloc_fail_after(0);
    ASSERT_EQ_INT(events_cause_read(&c), -1);
    test_alloc_fail_after(-1);
}

/* ---- hooks.log ---------------------------------------------------------------------------------
 */

typedef struct {
    char *db;
    char *log;
} LogCase;

/* A brain path whose folder holds <db>.d, and <db>.d/hooks.log as it will be. */
static LogCase given_brain_folder(void)
{
    LogCase lc;
    char *side = NULL;

    lc.db = make_temp_db_path();
    ASSERT_TRUE(lc.db != NULL);
    side = harness_sidecar_path(lc.db);
    harness_write_file(side, "x");
    free(side);
    lc.log = harness_side_path(lc.db, ".d/hooks.log");
    return lc;
}

/* The whole log text (heap), or NULL when there is no file. */
static char *read_log(const char *path)
{
    size_t len = 0;

    return harness_read_file(path, &len);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the type, then how often
static void count_origin_skips(EventAction *action, EventType type, int times)
{
    int i = 0;

    for (i = 0; i < times; i++) {
        events_count_skip(action, EVENT_SKIP_ORIGIN, type);
    }
}

TEST(skipped_source_lines_are_one_per_action_and_type_with_the_count)
{
    EventRules rules;
    LogCase lc = given_brain_folder();
    char *text = NULL;

    given_rules(&rules, ACTION(ALL_TYPES, "") ","
                                              "{\"name\":\"two\",\"events\":[\"memory.*\"],"
                                              "\"run\":{\"command\":[\"/bin/echo\"]}}");
    count_origin_skips(&rules.actions[0], EVENT_CREATED, 12);
    count_origin_skips(&rules.actions[0], EVENT_UPDATED, 1);
    count_origin_skips(&rules.actions[1], EVENT_DELETED, 3);
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "import", STORE_ORIGIN_IMPORT), 3);
    text = read_log(lc.log);
    ASSERT_TRUE(text != NULL);
    ASSERT_STR_PREFIX(text, "{\"version\":1,\"at\":\"20");
    ASSERT_STR_CONTAINS(text, "\",\"event\":\"skipped_source\",\"event_type\":\"memory.created\","
                              "\"command\":\"import\",\"origin\":\"import\",\"plugin\":\"hooker\","
                              "\"action\":\"go\",\"count\":12}\n{\"version\":1,\"at\":\"");
    ASSERT_STR_CONTAINS(text, "\"event_type\":\"memory.updated\",\"command\":\"import\","
                              "\"origin\":\"import\",\"plugin\":\"hooker\",\"action\":\"go\","
                              "\"count\":1}\n");
    ASSERT_STR_CONTAINS(text, "\"event_type\":\"memory.deleted\",\"command\":\"import\","
                              "\"origin\":\"import\",\"plugin\":\"hooker\",\"action\":\"two\","
                              "\"count\":3}\n");
    ASSERT_EQ_INT(rules.actions[0].skipped_source[EVENT_CREATED], 0);
    ASSERT_EQ_INT(rules.actions[1].skipped_source[EVENT_DELETED], 0);
    /* a second run with nothing counted writes nothing, and a later count is appended */
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "import", STORE_ORIGIN_IMPORT), 0);
    count_origin_skips(&rules.actions[1], EVENT_PURGED, 2);
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "sync", STORE_ORIGIN_SYNC), 1);
    free(text);
    text = read_log(lc.log);
    ASSERT_STR_CONTAINS(text, "\"event_type\":\"memory.purged\",\"command\":\"sync\","
                              "\"origin\":\"sync\",\"plugin\":\"hooker\",\"action\":\"two\","
                              "\"count\":2}\n");
    ASSERT_STR_CONTAINS(text, "\"count\":12}\n");
    free(text);
    events_rules_free(&rules);
    free(lc.db);
    free(lc.log);
}

TEST(a_local_write_logs_no_skipped_source_line_and_keeps_its_counts)
{
    EventRules rules;
    LogCase lc = given_brain_folder();

    given_rules(&rules, ACTION(ALL_TYPES, ""));
    count_origin_skips(&rules.actions[0], EVENT_CREATED, 2);
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "add", STORE_ORIGIN_LOCAL), 0);
    ASSERT_FALSE(access(lc.log, F_OK) == 0);
    ASSERT_EQ_INT(rules.actions[0].skipped_source[EVENT_CREATED], 2);
    events_rules_free(&rules);
    free(lc.db);
    free(lc.log);
}

TEST(nothing_counted_makes_no_log_file)
{
    EventRules rules;
    LogCase lc = given_brain_folder();

    given_rules(&rules, ACTION(ALL_TYPES, ""));
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "import", STORE_ORIGIN_IMPORT), 0);
    ASSERT_FALSE(access(lc.log, F_OK) == 0);
    events_rules_free(&rules);
    free(lc.db);
    free(lc.log);
}

TEST(the_log_is_mode_0600_and_a_log_that_cannot_be_written_is_not_an_error)
{
    EventRules rules;
    LogCase lc = given_brain_folder();
    struct stat st;
    char *elsewhere = harness_side_path(lc.db, ".d/elsewhere");

    given_rules(&rules, ACTION(ALL_TYPES, ""));
    count_origin_skips(&rules.actions[0], EVENT_CREATED, 1);
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "import", STORE_ORIGIN_IMPORT), 1);
    ASSERT_EQ_INT(stat(lc.log, &st), 0);
    ASSERT_EQ_INT((unsigned)st.st_mode & 0777U, 0600U);
    /* hooks.log is a symlink: the log is not followed, and the count is kept */
    ASSERT_EQ_INT(unlink(lc.log), 0);
    harness_write_file(elsewhere, "");
    ASSERT_EQ_INT(symlink(elsewhere, lc.log), 0);
    count_origin_skips(&rules.actions[0], EVENT_UPDATED, 4);
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "import", STORE_ORIGIN_IMPORT), 0);
    ASSERT_EQ_INT(rules.actions[0].skipped_source[EVENT_UPDATED], 4);
    /* no folder at all */
    ASSERT_EQ_INT(
        events_log_skipped(&rules, "/nonexistent-dir/x.db", "import", STORE_ORIGIN_IMPORT), 0);
    ASSERT_EQ_INT(rules.actions[0].skipped_source[EVENT_UPDATED], 4);
    free(elsewhere);
    events_rules_free(&rules);
    free(lc.db);
    free(lc.log);
}

/* The lowest free descriptor now: a leaked one moves it up (standard:c). */
static int lowest_free_fd(void)
{
    int fd = fcntl(0, F_DUPFD_CLOEXEC, 0);

    ASSERT_TRUE(fd >= 0);
    (void)close(fd);
    return fd;
}

TEST(a_fifo_at_hooks_log_neither_hangs_the_command_nor_takes_a_line)
{
    EventRules rules;
    LogCase lc = given_brain_folder();
    int before = 0;
    int reader = -1;

    given_rules(&rules, ACTION(ALL_TYPES, ""));
    ASSERT_EQ_INT(mkfifo(lc.log, 0600), 0);
    count_origin_skips(&rules.actions[0], EVENT_CREATED, 2);
    before = lowest_free_fd();
    /* no reader: a blocking open would wait for one forever */
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "import", STORE_ORIGIN_IMPORT), 0);
    ASSERT_EQ_INT(rules.actions[0].skipped_source[EVENT_CREATED], 2);
    ASSERT_EQ_INT(lowest_free_fd(), before);
    /* a reader is there, so the open works: the node is still not a log file */
    reader = open(lc.log, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    ASSERT_TRUE(reader >= 0);
    before = lowest_free_fd();
    ASSERT_EQ_INT(events_log_skipped(&rules, lc.db, "import", STORE_ORIGIN_IMPORT), 0);
    ASSERT_EQ_INT(rules.actions[0].skipped_source[EVENT_CREATED], 2);
    ASSERT_EQ_INT(lowest_free_fd(), before);
    ASSERT_EQ_INT(read(reader, &before, 1U), 0);
    (void)close(reader);
    events_rules_free(&rules);
    free(lc.db);
    free(lc.log);
}

/* ---- the store: events found by what a write changed ---------------------------------------- */

static const char k_now[] = "2026-11-18T12:00:00.000Z";

#define NAMED(name, events, filter)                                                                \
    "{\"name\":\"" name "\",\"events\":" events ",\"run\":{\"command\":[\"/bin/echo\"]}" filter "}"

typedef struct {
    Store *store;
    char *db;
    EventRules rules;
    EventCause cause;
} Rig;

typedef struct {
    const char *actions; /* the actions of plugin hooker; NULL is no plugin at all */
    StoreOrigin origin;
    StoreHooks hooks;
} RigSpec;

static void rig_arm(Rig *rig, RigSpec spec)
{
    StoreEvents cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.rules = &rig->rules;
    cfg.cause = &rig->cause;
    cfg.command = "add";
    cfg.brain_path = "~/t.db";
    cfg.origin = spec.origin;
    cfg.hooks = spec.hooks;
    ASSERT_EQ_STATUS(store_set_events(rig->store, &cfg), STORE_OK);
}

static Rig given_rig(RigSpec spec)
{
    Rig rig;
    char err[ERR_BUFSIZE];

    memset(&rig, 0, sizeof(rig));
    clear_cause_env();
    rig.db = make_temp_db_path();
    ASSERT_TRUE(rig.db != NULL);
    rig.store = store_open(rig.db, err, sizeof(err));
    ASSERT_TRUE(rig.store != NULL);
    if (spec.actions != NULL) {
        given_rules(&rig.rules, spec.actions);
    }
    ASSERT_EQ_INT(events_cause_read(&rig.cause), 0);
    rig_arm(&rig, spec);
    return rig;
}

static Rig given_rig_all(void)
{
    return given_rig((RigSpec){.actions = ACTION(ALL_TYPES, "")});
}

static void rig_close(Rig *rig)
{
    store_close(rig->store);
    events_rules_free(&rig->rules);
    events_cause_free(&rig->cause);
    free(rig->db);
}

/* One line of a query on the brain file, compared with want. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the query, then its answer
static void assert_db(const Rig *rig, const char *sql, const char *want)
{
    char *got = harness_sqlite_query_line(rig->db, sql);

    ASSERT_TRUE(got != NULL);
    ASSERT_STREQ(got != NULL ? got : "", want);
    free(got);
}

static void assert_outbox_types(const Rig *rig, const char *want)
{
    assert_db(rig,
              "SELECT COALESCE(group_concat(type, ','), 'none') FROM "
              "(SELECT type FROM outbox ORDER BY rowid);",
              want);
}

static void clear_outbox(const Rig *rig)
{
    free(harness_sqlite_query_line(rig->db, "DELETE FROM outbox;"));
}

static long long add_memory(Rig *rig, const char *body, const char *key)
{
    static int counter = 0;
    char hash[65];
    StoreAddAction act = STORE_ADD_CREATED;
    Entry e;
    long long id = 0;

    memset(&e, 0, sizeof(e));
    counter++;
    (void)snprintf(hash, sizeof(hash), "%064x", (unsigned)counter);
    ASSERT_EQ_STATUS(
        store_add(rig->store, body, hash, key, NULL, 0U, "human", NULL, k_now, &act, &e), STORE_OK);
    id = e.id;
    store_entry_free(&e);
    return id;
}

static StoreStatus update_memory(Rig *rig, long long id, StoreUpdate u)
{
    Entry e;
    long long conflict = 0;
    StoreStatus st = STORE_OK;

    memset(&e, 0, sizeof(e));
    u.actor = u.actor != NULL ? u.actor : "human";
    st = store_update(rig->store, id, &u, k_now, &e, &conflict);
    store_entry_free(&e);
    return st;
}

static void update_body(Rig *rig, long long id, const char *body)
{
    char hash[65];
    static int counter = 100000;

    counter++;
    (void)snprintf(hash, sizeof(hash), "%064x", (unsigned)counter);
    ASSERT_EQ_STATUS(update_memory(rig, id,
                                   (StoreUpdate){.body = body,
                                                 .body_hash = hash,
                                                 .set_body = true,
                                                 .bin = STORE_BIN_LIVE}),
                     STORE_OK);
}

static void delete_memory(Rig *rig, long long id)
{
    Entry e;

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_soft_delete_by_id(rig->store, id, "human", k_now, &e), STORE_OK);
    store_entry_free(&e);
}

static void restore_memory(Rig *rig, long long id)
{
    ASSERT_EQ_STATUS(
        update_memory(rig, id, (StoreUpdate){.undelete = true, .bin = STORE_BIN_DELETED}),
        STORE_OK);
}

static void purge_memory(Rig *rig, long long id)
{
    Entry e;

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_hard_delete_by_id(rig->store, id, STORE_BIN_DELETED, k_now, &e),
                     STORE_OK);
    store_entry_free(&e);
}

static void link_memories(Rig *rig, long long from, long long to, StoreEdgeKind kind)
{
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;

    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(rig->store, (StoreEdge){.from_id = from, .to_id = to}, kind,
                                "human", k_now, &act, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
}

static void unlink_memories(Rig *rig, long long from, long long to)
{
    StoreNeighbor *stubs = NULL;
    size_t n = 0;

    ASSERT_EQ_STATUS(store_unlink(rig->store, from, to, NULL, "human", k_now, &stubs, &n),
                     STORE_OK);
    store_neighbors_free(stubs, n);
}

TEST(six_event_types_created)
{
    Rig rig = given_rig_all();
    long long id = add_memory(&rig, "first body", "k:one");

    update_body(&rig, id, "second body");
    delete_memory(&rig, id);
    restore_memory(&rig, id);
    delete_memory(&rig, id);
    purge_memory(&rig, id);
    assert_outbox_types(&rig, "memory.created,memory.updated,memory.deleted,memory.restored,"
                              "memory.deleted,memory.purged");
    assert_db(&rig, "SELECT count(DISTINCT event_id) FROM outbox;", "6");
    rig_close(&rig);
}

TEST(a_link_writes_two_updated_events_with_one_batch_and_no_vector_change)
{
    Rig rig = given_rig_all();
    long long a = add_memory(&rig, "body a", "k:a");
    long long b = add_memory(&rig, "body b", "k:b");
    char *vv_before = harness_sqlite_query_line(rig.db, "SELECT group_concat(version_vector, '|') "
                                                        "FROM (SELECT version_vector FROM entries "
                                                        "ORDER BY local_id);");
    char *vv_after = NULL;

    clear_outbox(&rig);
    link_memories(&rig, a, b, STORE_EDGE_CITES);
    assert_outbox_types(&rig, "memory.updated,memory.updated");
    assert_db(&rig, "SELECT count(DISTINCT json_extract(payload, '$.batch_id')) FROM outbox;", "1");
    assert_db(&rig,
              "SELECT group_concat(json_extract(payload, '$.memory.key') || ':' || "
              "json_extract(payload, '$.changes.link.direction') || ':' || "
              "json_extract(payload, '$.changes.link.other.key'), ',') FROM "
              "(SELECT payload FROM outbox ORDER BY rowid);",
              "k:a:outgoing:k:b,k:b:incoming:k:a");
    assert_db(&rig,
              "SELECT group_concat(DISTINCT json_extract(payload, '$.changes.link.kind')) "
              "FROM outbox;",
              "cites");
    assert_db(&rig,
              "SELECT group_concat(json_array_length(json_extract(payload, '$.memory.links')))"
              " FROM outbox;",
              "1,1");
    vv_after = harness_sqlite_query_line(rig.db, "SELECT group_concat(version_vector, '|') "
                                                 "FROM (SELECT version_vector FROM entries "
                                                 "ORDER BY local_id);");
    ASSERT_TRUE(vv_before != NULL && vv_after != NULL);
    ASSERT_STREQ(vv_before != NULL ? vv_before : "", vv_after != NULL ? vv_after : "x");
    assert_db(&rig,
              "SELECT count(*) FROM outbox o JOIN entries e ON e.id = "
              "json_extract(o.payload, '$.memory.id') WHERE "
              "json_extract(o.payload, '$.memory.change_seq') = e.change_seq;",
              "2");
    free(vv_before);
    free(vv_after);
    clear_outbox(&rig);
    unlink_memories(&rig, a, b);
    assert_outbox_types(&rig, "memory.updated,memory.updated");
    assert_db(&rig,
              "SELECT group_concat(json_array_length(json_extract(payload, '$.memory.links')))"
              " FROM outbox;",
              "0,0");
    clear_outbox(&rig);
    link_memories(&rig, a, b, STORE_EDGE_RELATED);
    assert_db(&rig,
              "SELECT group_concat(json_extract(payload, '$.changes.link.direction')) "
              "FROM outbox;",
              "both,both");
    rig_close(&rig);
}

TEST(a_link_to_a_memory_that_is_purged_updates_the_neighbour_too)
{
    Rig rig = given_rig_all();
    long long a = add_memory(&rig, "body a", "k:a");
    long long b = add_memory(&rig, "body b", "k:b");

    link_memories(&rig, a, b, STORE_EDGE_RELATED);
    delete_memory(&rig, a);
    clear_outbox(&rig);
    purge_memory(&rig, a);
    assert_outbox_types(&rig, "memory.purged,memory.updated");
    assert_db(&rig,
              "SELECT json_extract(payload, '$.memory.key') || ':' || "
              "json_extract(payload, '$.changes.link.other.key') FROM outbox "
              "WHERE type = 'memory.updated';",
              "k:b:k:a");
    rig_close(&rig);
}

/* A whole-bin purge flushes thousands of memories; a scan for each one is quadratic (20000
   tagged memories took 30 s). */
TEST(the_flush_finds_each_memory_s_old_tags_and_links_by_an_index)
{
    Rig rig = given_rig_all();

    ASSERT_EQ_INT(store_test_event_read_scans(rig.store), 0);
    rig_close(&rig);
}

TEST(no_enabled_action_no_row)
{
    Rig none = given_rig((RigSpec){.actions = NULL});
    Rig other_event = given_rig((RigSpec){.actions = ACTION("[\"memory.purged\"]", "")});
    long long id = 0;

    id = add_memory(&none, "body", NULL);
    update_body(&none, id, "again");
    delete_memory(&none, id);
    assert_outbox_types(&none, "none");
    id = add_memory(&other_event, "body", NULL);
    update_body(&other_event, id, "again");
    assert_outbox_types(&other_event, "none");
    rig_close(&none);
    rig_close(&other_event);
}

TEST(an_event_with_no_matching_action_is_not_even_built)
{
    Rig rig =
        given_rig((RigSpec){.actions = ACTION(CREATED_ONLY, FILTER("\"tags\":{\"any\":[\"x\"]}"))});
    long long id = add_memory(&rig, "body", "k:1");

    assert_outbox_types(&rig, "none");
    update_body(&rig, id, "again");
    assert_outbox_types(&rig, "none");
    rig_close(&rig);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the body, then the tag
static void add_tagged(Rig *rig, const char *body, const char *tag)
{
    static int counter = 500000;
    char hash[65];
    const char *tags[] = {tag};
    StoreAddAction act = STORE_ADD_CREATED;
    Entry e;

    memset(&e, 0, sizeof(e));
    counter++;
    (void)snprintf(hash, sizeof(hash), "%064x", (unsigned)counter);
    ASSERT_EQ_STATUS(
        store_add(rig->store, body, hash, NULL, tags, 1U, "agent", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
}

TEST(filter_miss_inserts_nothing_counts_skipped)
{
    Rig rig =
        given_rig((RigSpec){.actions = ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"want\"]}"))});

    add_tagged(&rig, "one", "other");
    add_tagged(&rig, "two", "other");
    assert_outbox_types(&rig, "none");
    ASSERT_EQ_INT(rig.rules.actions[0].skipped_filter, 2);
    add_tagged(&rig, "three", "want");
    assert_outbox_types(&rig, "memory.created");
    ASSERT_EQ_INT(rig.rules.actions[0].skipped_filter, 2);
    assert_db(&rig, "SELECT recipients FROM outbox;",
              "[{\"entry\":\"plugin/hooker/go\",\"state\":\"pending\"}]");
    rig_close(&rig);
}

/* created_by reads the memory's creator, updated_by its last writer: a human's memory that an
   agent edits matches both. */
TEST(created_by_is_the_creator_even_after_another_kind_writes)
{
    Rig rig = given_rig((RigSpec){
        .actions = ACTION("[\"memory.updated\"]",
                          FILTER("\"created_by\":[\"human\"],\"updated_by\":[\"agent\"]"))});
    long long id = add_memory(&rig, "mine", NULL);
    char hash[65];

    (void)snprintf(hash, sizeof(hash), "%064x", 0xc0ffeeU);
    ASSERT_EQ_STATUS(update_memory(&rig, id,
                                   (StoreUpdate){.body = "edited",
                                                 .body_hash = hash,
                                                 .set_body = true,
                                                 .bin = STORE_BIN_LIVE,
                                                 .actor = "agent"}),
                     STORE_OK);
    assert_outbox_types(&rig, "memory.updated");
    rig_close(&rig);
}

TEST(ten_actions_of_one_plugin_are_ten_rules)
{
    EventRules rules;

    given_rules(
        &rules,
        NAMED("a0", ALL_TYPES, "") "," NAMED("a1", ALL_TYPES, "") "," NAMED("a2", ALL_TYPES, "") "," NAMED(
            "a3", ALL_TYPES,
            "") "," NAMED("a4", ALL_TYPES,
                          "") "," NAMED("a5", ALL_TYPES,
                                        "") "," NAMED("a6", ALL_TYPES,
                                                      "") "," NAMED("a7", ALL_TYPES,
                                                                    "") "," NAMED("a8", ALL_TYPES,
                                                                                  "") "," NAMED("a"
                                                                                                "9",
                                                                                                ALL_TYPES,
                                                                                                ""));
    ASSERT_EQ_INT(rules.count, 10);
    ASSERT_STREQ(rules.count == 10U ? rules.actions[9].recipient : "", "plugin/hooker/a9");
    events_rules_free(&rules);
}

TEST(each_action_gets_the_row_it_matches_and_the_recipients_list_only_those)
{
    Rig rig = given_rig((RigSpec){
        .actions = NAMED("go", ALL_TYPES, FILTER("\"tags\":{\"any\":[\"want\"]}")) "," NAMED(
            "two", ALL_TYPES, "") "," NAMED("three", ALL_TYPES,
                                            FILTER("\"created_by\":[\"human\"]"))});

    add_tagged(&rig, "one", "other"); /* actor agent: only "two" matches */
    assert_db(&rig, "SELECT recipients FROM outbox;",
              "[{\"entry\":\"plugin/hooker/two\",\"state\":\"pending\"}]");
    add_tagged(&rig, "two", "want");
    assert_db(&rig, "SELECT recipients FROM outbox ORDER BY rowid DESC LIMIT 1;",
              "[{\"entry\":\"plugin/hooker/go\",\"state\":\"pending\"},"
              "{\"entry\":\"plugin/hooker/two\",\"state\":\"pending\"}]");
    ASSERT_EQ_INT(rig.rules.actions[0].skipped_filter, 1);
    ASSERT_EQ_INT(rig.rules.actions[1].skipped_filter, 0);
    ASSERT_EQ_INT(rig.rules.actions[2].skipped_filter, 2);
    rig_close(&rig);
}

TEST(default_origins_skip_sync_and_import_and_count_skipped_source)
{
    StoreOrigin origin = STORE_ORIGIN_SYNC;

    for (origin = STORE_ORIGIN_SYNC; origin <= STORE_ORIGIN_IMPORT; origin++) {
        Rig rig = given_rig((RigSpec){.actions = ACTION(ALL_TYPES, ""), .origin = origin});
        long long id = add_memory(&rig, "body", "k:1");

        update_body(&rig, id, "again");
        assert_outbox_types(&rig, "none");
        ASSERT_EQ_INT(rig.rules.actions[0].skipped_source[EVENT_CREATED], 1);
        ASSERT_EQ_INT(rig.rules.actions[0].skipped_source[EVENT_UPDATED], 1);
        ASSERT_EQ_INT(rig.rules.actions[0].skipped_filter, 0);
        rig_close(&rig);
    }
}

TEST(an_action_that_lists_the_origin_gets_the_event_with_that_origin)
{
    Rig rig = given_rig(
        (RigSpec){.actions = ACTION(ALL_TYPES, FILTER("\"origins\":[\"local\",\"sync\"]")),
                  .origin = STORE_ORIGIN_SYNC});

    add_memory(&rig, "body", "k:1");
    assert_outbox_types(&rig, "memory.created");
    assert_db(&rig, "SELECT json_extract(payload, '$.cause.origin') FROM outbox;", "sync");
    rig_close(&rig);
}

TEST(hooks_flag_ignores_origins)
{
    Rig rig = given_rig((RigSpec){.actions = ACTION(ALL_TYPES, FILTER("\"origins\":[\"sync\"]")),
                                  .origin = STORE_ORIGIN_LOCAL,
                                  .hooks = STORE_HOOKS_ON});

    add_memory(&rig, "body", "k:1");
    assert_outbox_types(&rig, "memory.created");
    assert_db(&rig, "SELECT json_extract(payload, '$.cause.origin') FROM outbox;", "local");
    ASSERT_EQ_INT(rig.rules.actions[0].skipped_source[EVENT_CREATED], 0);
    store_set_event_hooks(rig.store, STORE_HOOKS_DEFAULT);
    add_memory(&rig, "body two", "k:2");
    assert_outbox_types(&rig, "memory.created"); /* the second write is a local one: not sent */
    ASSERT_EQ_INT(rig.rules.actions[0].skipped_source[EVENT_CREATED], 1);
    rig_close(&rig);
}

TEST(no_hooks_inserts_nothing)
{
    Rig rig = given_rig((RigSpec){.actions = ACTION(ALL_TYPES, ""), .hooks = STORE_HOOKS_OFF});
    long long id = add_memory(&rig, "body", "k:1");

    update_body(&rig, id, "again");
    delete_memory(&rig, id);
    assert_outbox_types(&rig, "none");
    ASSERT_EQ_INT(rig.rules.actions[0].skipped_filter, 0);
    ASSERT_EQ_INT(rig.rules.actions[0].skipped_source[EVENT_CREATED], 0);
    store_set_event_hooks(rig.store, STORE_HOOKS_DEFAULT);
    add_memory(&rig, "body two", "k:2");
    assert_outbox_types(&rig, "memory.created");
    rig_close(&rig);
}

TEST(hooks_on_does_not_ignore_a_filter_other_than_origins)
{
    Rig rig =
        given_rig((RigSpec){.actions = ACTION(ALL_TYPES, FILTER("\"tags\":{\"any\":[\"want\"]}")),
                            .hooks = STORE_HOOKS_ON});

    add_tagged(&rig, "one", "other");
    assert_outbox_types(&rig, "none");
    rig_close(&rig);
}

TEST(with_processes_off_a_matching_write_inserts_no_row_and_is_counted)
{
    Rig rig = given_rig((RigSpec){.actions = ACTION(CREATED_ONLY, "")});
    long long id = 0;

    events_set_processes(false);
    id = add_memory(&rig, "body", "k:1");
    update_body(&rig, id, "again"); /* an event no action lists: not counted */
    add_memory(&rig, "body two", "k:2");
    events_set_processes(true);
    assert_outbox_types(&rig, "none");
    ASSERT_EQ_INT(store_events_unsupported(rig.store), 2);
    rig_close(&rig);
}

TEST(an_unchanged_write_makes_no_event)
{
    Rig rig = given_rig_all();
    long long id = add_memory(&rig, "body", "k:1");
    char hash[65];
    StoreAddAction act = STORE_ADD_CREATED;
    Entry e;

    clear_outbox(&rig);
    memset(&e, 0, sizeof(e));
    (void)snprintf(hash, sizeof(hash), "%064x", (unsigned)1);
    /* the same key with the same body: merges into the slot and changes nothing */
    ASSERT_EQ_STATUS(
        store_add(rig.store, "body", hash, "k:1", NULL, 0U, "agent", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_EQ_INT(e.id, id);
    store_entry_free(&e);
    assert_outbox_types(&rig, "none");
    rig_close(&rig);
}

TEST(a_tag_change_alone_is_one_updated_event_with_the_tags_difference)
{
    Rig rig = given_rig_all();
    long long id = add_memory(&rig, "body", "k:1");
    const char *tags[] = {"fresh"};

    clear_outbox(&rig);
    ASSERT_EQ_STATUS(
        update_memory(
            &rig, id,
            (StoreUpdate){.tags = tags, .ntags = 1U, .set_tags = true, .bin = STORE_BIN_LIVE}),
        STORE_OK);
    assert_outbox_types(&rig, "memory.updated");
    assert_db(&rig, "SELECT json_extract(payload, '$.changes') FROM outbox;",
              "{\"fields\":[\"tags\"],\"before\":{\"tags\":[]},\"tags_added\":[\"fresh\"],"
              "\"tags_removed\":[]}");
    assert_db(&rig, "SELECT json_extract(payload, '$.memory.tags') FROM outbox;", "[\"fresh\"]");
    rig_close(&rig);
}

TEST(a_failed_write_leaves_no_trace_in_the_next_one)
{
    Rig rig = given_rig_all();
    long long a = add_memory(&rig, "body a", "k:a");
    long long b = add_memory(&rig, "body b", "k:b");
    char hash[65];

    clear_outbox(&rig);
    (void)snprintf(hash, sizeof(hash), "%064x", (unsigned)777);
    /* a stale version vector: the write rolls back after it has looked at the row */
    ASSERT_EQ_STATUS(update_memory(&rig, a,
                                   (StoreUpdate){.body = "never",
                                                 .body_hash = hash,
                                                 .set_body = true,
                                                 .if_version_vector = "{}",
                                                 .bin = STORE_BIN_LIVE}),
                     STORE_ERR_STALE);
    assert_outbox_types(&rig, "none");
    update_body(&rig, b, "good");
    assert_outbox_types(&rig, "memory.updated");
    assert_db(&rig, "SELECT json_extract(payload, '$.memory.key') FROM outbox;", "k:b");
    rig_close(&rig);
}

TEST(a_whole_bin_purge_writes_one_purged_event_per_row_with_its_last_state)
{
    Rig rig = given_rig_all();
    long long a = add_memory(&rig, "body a", "k:a");
    long long b = add_memory(&rig, "body b", "k:b");
    Entry *rows = NULL;
    size_t n = 0;
    size_t i = 0;

    delete_memory(&rig, a);
    delete_memory(&rig, b);
    clear_outbox(&rig);
    ASSERT_EQ_STATUS(store_purge(rig.store, STORE_BIN_DELETED, k_now, &rows, &n), STORE_OK);
    for (i = 0; i < n; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);
    ASSERT_EQ_INT(n, 2);
    assert_outbox_types(&rig, "memory.purged,memory.purged");
    assert_db(&rig,
              "SELECT group_concat(json_extract(payload, '$.memory.body') || ':' || "
              "json_extract(payload, '$.memory.bin'), ',') FROM (SELECT payload FROM outbox "
              "ORDER BY rowid);",
              "body a:deleted,body b:deleted");
    rig_close(&rig);
}

TEST(the_row_and_its_payload_have_the_shape_plan_35_reads)
{
    Rig rig = given_rig_all();
    char *device = NULL;
    char sql[512];

    add_memory(&rig, "body", "k:1");
    assert_db(&rig,
              "SELECT length(event_id) || ':' || type || ':' || length(created_at) FROM outbox;",
              "36:memory.created:24");
    assert_db(&rig, "SELECT json_extract(payload, '$.event_id') = event_id FROM outbox;", "1");
    assert_db(&rig, "SELECT json_extract(payload, '$.event_type') FROM outbox;", "memory.created");
    assert_db(&rig,
              "SELECT json_extract(payload, '$.occurred_at') = created_at AND "
              "json_extract(payload, '$.detected_at') = created_at FROM outbox;",
              "1");
    assert_db(&rig,
              "SELECT json_extract(payload, '$.cause.command') || ':' || "
              "json_extract(payload, '$.cause.depth') FROM outbox;",
              "add:0");
    assert_db(&rig, "SELECT json_extract(payload, '$.brain.path') FROM outbox;", "~/t.db");
    assert_db(&rig,
              "SELECT json_extract(payload, '$.memory.change_seq') = (SELECT change_seq "
              "FROM entries) FROM outbox;",
              "1");
    assert_db(&rig,
              "SELECT json_extract(payload, '$.memory.id') = (SELECT id FROM entries) "
              "FROM outbox;",
              "1");
    assert_db(&rig,
              "SELECT json_type(payload, '$.memory.local_id') IS NULL AND "
              "json_type(payload, '$.memory.version_vector') IS NULL FROM outbox;",
              "1");
    assert_db(&rig,
              "SELECT json_type(payload, '$.plugin') IS NULL AND json_type(payload, '$.entry') "
              "IS NULL AND json_type(payload, '$.data_dir') IS NULL AND "
              "json_type(payload, '$.settings') IS NULL FROM outbox;",
              "1");
    assert_db(&rig,
              "SELECT json_extract(payload, '$.limits.max_bytes') || ':' || "
              "json_extract(payload, '$.truncated') FROM outbox;",
              "262144:0");
    device = harness_sqlite_query_line(rig.db, "SELECT json_extract(payload, '$.brain.device_id') "
                                               "FROM outbox;");
    ASSERT_TRUE(device != NULL);
    (void)snprintf(sql, sizeof(sql), "SELECT '%s' = '%s';", device != NULL ? device : "",
                   store_device_id(rig.store));
    assert_db(&rig, sql, "1");
    free(device);
    rig_close(&rig);
}

TEST(no_body_or_tag_text_is_in_a_column_a_dispatcher_puts_in_argv_or_env)
{
    Rig rig = given_rig_all();

    add_tagged(&rig, "secret-body-text", "secret-tag-text");
    assert_db(&rig,
              "SELECT instr(event_id || type || recipients || created_at, 'secret')"
              " FROM outbox;",
              "0");
    assert_db(&rig,
              "SELECT instr(payload, 'secret-body-text') > 0 AND "
              "instr(payload, 'secret-tag-text') > 0 FROM outbox;",
              "1");
    rig_close(&rig);
}

TEST(one_command_shares_one_batch_id_and_a_new_command_has_its_own)
{
    Rig rig = given_rig_all();

    add_memory(&rig, "one", "k:1");
    add_memory(&rig, "two", "k:2");
    assert_db(&rig, "SELECT count(DISTINCT json_extract(payload, '$.batch_id')) FROM outbox;", "1");
    assert_db(&rig, "SELECT length(json_extract(payload, '$.batch_id')) FROM outbox LIMIT 1;",
              "36");
    rig_arm(&rig, (RigSpec){.actions = NULL});
    add_memory(&rig, "three", "k:3");
    assert_db(&rig, "SELECT count(DISTINCT json_extract(payload, '$.batch_id')) FROM outbox;", "2");
    rig_close(&rig);
}

TEST(a_write_inside_a_run_carries_the_hook_cause_and_not_caused_by_skips_it)
{
    Rig rig = given_rig((RigSpec){
        .actions = NAMED("go", ALL_TYPES, FILTER("\"not_caused_by\":[\"url_expand\"]")) "," NAMED(
            "two", ALL_TYPES, "")});

    /* given_rig made a direct cause; a write in a run reads the run's variables instead */
    events_cause_free(&rig.cause);
    given_run_env("[]");
    ASSERT_EQ_INT(events_cause_read(&rig.cause), 0);
    add_memory(&rig, "body", "k:1");
    assert_db(&rig, "SELECT recipients FROM outbox;",
              "[{\"entry\":\"plugin/hooker/two\",\"state\":\"pending\"}]");
    assert_db(&rig,
              "SELECT json_extract(payload, '$.cause.hook') || ':' || "
              "json_extract(payload, '$.cause.depth') || ':' || "
              "json_array_length(json_extract(payload, '$.cause.chain')) FROM outbox;",
              "plugin/url_expand/fetch:2:1");
    clear_cause_env();
    rig_close(&rig);
}

TEST(bins_and_match_before_read_the_state_before_the_write)
{
    Rig deleted_only =
        given_rig((RigSpec){.actions = ACTION(ALL_TYPES, FILTER("\"bins\":[\"deleted\"]"))});
    Rig tags_before = given_rig((RigSpec){
        .actions = ACTION("[\"memory.updated\"]",
                          FILTER("\"tags\":{\"any\":[\"todo\"],\"match_before\":true}"))});
    Rig tags_after = given_rig((RigSpec){
        .actions = ACTION("[\"memory.updated\"]", FILTER("\"tags\":{\"any\":[\"todo\"]}"))});
    long long id = add_memory(&deleted_only, "body", "k:1");
    const char *todo[] = {"todo"};
    StoreAddAction act = STORE_ADD_CREATED;
    Entry e;
    char hash[65];

    delete_memory(&deleted_only, id);
    restore_memory(&deleted_only, id);
    assert_outbox_types(&deleted_only, "memory.restored");
    memset(&e, 0, sizeof(e));
    (void)snprintf(hash, sizeof(hash), "%064x", (unsigned)999);
    ASSERT_EQ_STATUS(
        store_add(tags_before.store, "body", hash, "k:1", todo, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    id = e.id;
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(tags_after.store, "body", hash, "k:1", todo, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    /* drop the tag: the tags before had it */
    ASSERT_EQ_STATUS(
        update_memory(&tags_before, id, (StoreUpdate){.set_tags = true, .bin = STORE_BIN_LIVE}),
        STORE_OK);
    ASSERT_EQ_STATUS(
        update_memory(&tags_after, id, (StoreUpdate){.set_tags = true, .bin = STORE_BIN_LIVE}),
        STORE_OK);
    assert_outbox_types(&tags_before, "memory.updated");
    assert_outbox_types(&tags_after, "none");
    rig_close(&deleted_only);
    rig_close(&tags_before);
    rig_close(&tags_after);
}

/* Counts of memories and outbox rows: they must always be equal after one add. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) failure seen, then success seen
static void assert_memory_and_row_agree(const Rig *rig, bool *saw_failure, bool *saw_success)
{
    char *got = harness_sqlite_query_line(
        rig->db, "SELECT (SELECT count(*) FROM entries) || '/' || (SELECT count(*) FROM outbox);");

    ASSERT_TRUE(got != NULL);
    if (got != NULL && strcmp(got, "0/0") == 0) {
        *saw_failure = true;
    } else if (got != NULL && strcmp(got, "1/1") == 0) {
        *saw_success = true;
    } else {
        ASSERT_STREQ(got != NULL ? got : "", "0/0 or 1/1");
    }
    free(got);
}

typedef void (*FailFn)(int);

/* An update reads the memory's old row, tags and links into the flush: whichever allocation or
   step fails, the new body and its outbox row commit together or not at all. */
TEST(an_update_of_a_tagged_linked_memory_commits_with_its_row_or_not_at_all)
{
    static const FailFn k_fail[] = {store_test_fail_alloc_after, store_test_fail_step_after};
    static const char *const tags[] = {"t"};
    bool saw_failure = false;
    bool saw_success = false;
    size_t which = 0;

    for (which = 0; which < sizeof(k_fail) / sizeof(k_fail[0]); which++) {
        for (int at = 0; at < 80; at++) {
            Rig rig = given_rig_all();
            long long a = add_memory(&rig, "body a", "k:a");
            long long b = add_memory(&rig, "body b", "k:b");
            char hash[65];
            char sql[160];
            char *got = NULL;
            StoreStatus st = STORE_OK;

            ASSERT_EQ_STATUS(update_memory(&rig, a,
                                           (StoreUpdate){.tags = tags,
                                                         .ntags = 1U,
                                                         .set_tags = true,
                                                         .bin = STORE_BIN_LIVE}),
                             STORE_OK);
            link_memories(&rig, a, b, STORE_EDGE_CITES);
            clear_outbox(&rig);
            (void)snprintf(hash, sizeof(hash), "%064x", 0xbeefU);
            k_fail[which](at);
            st = update_memory(
                &rig, a,
                (StoreUpdate){
                    .body = "edited", .body_hash = hash, .set_body = true, .bin = STORE_BIN_LIVE});
            k_fail[which](-1);
            (void)snprintf(sql, sizeof(sql),
                           "SELECT (SELECT body FROM entries WHERE local_id = %lld) || '/' || "
                           "(SELECT count(*) FROM outbox);",
                           a);
            got = harness_sqlite_query_line(rig.db, sql);
            /* A failure after COMMIT (reading the entry back) reports an error over a saved write,
               so a failure may show either state, never one without the other. */
            if (st == STORE_OK || (got != NULL && strcmp(got, "edited/1") == 0)) {
                ASSERT_STREQ(got != NULL ? got : "", "edited/1");
            } else {
                ASSERT_STREQ(got != NULL ? got : "", "body a/0");
            }
            saw_success = saw_success || st == STORE_OK;
            saw_failure = saw_failure || st != STORE_OK;
            free(got);
            rig_close(&rig);
        }
    }
    ASSERT_TRUE(saw_failure);
    ASSERT_TRUE(saw_success);
}

TEST(outbox_commits_with_memory)
{
    static const FailFn k_fail[] = {store_test_fail_exec_after, store_test_fail_step_after,
                                    store_test_fail_prepare_after, store_test_fail_alloc_after,
                                    test_alloc_fail_after};
    bool saw_failure = false;
    bool saw_success = false;
    size_t which = 0;

    for (which = 0; which < sizeof(k_fail) / sizeof(k_fail[0]); which++) {
        for (int at = 0; at < 60; at++) {
            Rig rig = given_rig_all();
            char hash[65];
            StoreAddAction act = STORE_ADD_CREATED;
            Entry e;

            memset(&e, 0, sizeof(e));
            (void)snprintf(hash, sizeof(hash), "%064x", (unsigned)4242);
            k_fail[which](at);
            (void)store_add(rig.store, "body", hash, "k:1", NULL, 0U, "human", NULL, k_now, &act,
                            &e);
            k_fail[which](-1);
            store_entry_free(&e);
            assert_memory_and_row_agree(&rig, &saw_failure, &saw_success);
            rig_close(&rig);
        }
    }
    ASSERT_TRUE(saw_failure);
    ASSERT_TRUE(saw_success);
}

/* The tests below drive the CLI and remember_run; the store test binary has neither. */
#ifdef REMEMBER_PLUGINS_DIAG_TEST

/* ---- the command line: plugins on disk, enabled in the brain, written through the CLI ----------
 */

#define RUN(db, ...)                                                                               \
    run_args((db), (const char *const[]){__VA_ARGS__},                                             \
             sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *))

static CmdResult run_args(const char *db, const char *const *args, size_t count)
{
    return run_remember(db, args, count, NULL);
}

/* The plugins folder this test's data dir holds: $XDG_DATA_HOME/remember/plugins. */
static char *given_plugins_folder(void)
{
    const char *xdg = getenv("XDG_DATA_HOME"); // NOLINT(concurrency-mt-unsafe)
    size_t cap = (xdg != NULL ? strlen(xdg) : 0U) + sizeof("/remember/plugins/hooker/plugin.json");
    char *path = (char *)malloc(cap);

    ASSERT_TRUE(xdg != NULL && path != NULL);
    if (xdg == NULL || path == NULL) {
        return (char *)calloc(1U, 1U);
    }
    (void)snprintf(path, cap, "%s/remember/plugins/hooker/plugin.json", xdg);
    return path;
}

/* Install plugin hooker with these actions (the text between the brackets). */
static void given_installed_plugin(const char *actions)
{
    char doc[8192];
    char *path = given_plugins_folder();

    (void)snprintf(doc, sizeof(doc),
                   "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"hooker\","
                   "\"plugin_version\":\"1.0.0\",\"protocol\":1,\"actions\":[%s]}",
                   actions);
    harness_write_file(path, doc);
    free(path);
}

static void assert_ok(CmdResult r)
{
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
}

/* A new brain where plugin hooker is installed and turned on for this brain. */
static char *given_brain_with_enabled_plugin(const char *actions)
{
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    given_installed_plugin(actions);
    assert_ok(RUN(db, "settings", "set", "plugin.hooker.enabled", "true", "--scope", "brain"));
    return db;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, then the row types
static void assert_rows(const char *db, const char *want)
{
    char *got =
        harness_sqlite_query_line(db, "SELECT COALESCE(group_concat(type, ','), 'none') FROM "
                                      "(SELECT type FROM outbox ORDER BY rowid);");

    ASSERT_TRUE(got != NULL);
    ASSERT_STREQ(got != NULL ? got : "", want);
    free(got);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, the query, then its answer
static void assert_query(const char *db, const char *sql, const char *want)
{
    char *got = harness_sqlite_query_line(db, sql);

    ASSERT_TRUE(got != NULL);
    ASSERT_STREQ(got != NULL ? got : "", want);
    free(got);
}

TEST(a_plugin_gets_rows_only_once_it_is_enabled_for_the_brain)
{
    char *db = make_temp_db_path();

    given_installed_plugin(ACTION(ALL_TYPES, ""));
    assert_ok(RUN(db, "add", "before"));
    assert_rows(db, "none");
    assert_ok(RUN(db, "settings", "set", "plugin.hooker.enabled", "true", "--scope", "brain"));
    assert_ok(RUN(db, "add", "after"));
    assert_rows(db, "memory.created");
    assert_ok(RUN(db, "settings", "set", "plugin.hooker.enabled", "false", "--scope", "brain"));
    assert_ok(RUN(db, "add", "later"));
    assert_rows(db, "memory.created");
    free(db);
}

TEST(a_plugin_enabled_in_one_brain_does_not_give_rows_in_another)
{
    char *one = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
    char *two = make_temp_db_path();

    assert_ok(RUN(two, "add", "other brain"));
    assert_rows(two, "none");
    assert_ok(RUN(one, "add", "this brain"));
    assert_rows(one, "memory.created");
    free(one);
    free(two);
}

TEST(a_plugin_with_only_search_filters_gets_no_rows)
{
    char *db = make_temp_db_path();
    char *path = given_plugins_folder();

    harness_write_file(
        path, "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"hooker\","
              "\"plugin_version\":\"1.0.0\",\"protocol\":1,\"filters\":[{\"name\":\"rank\","
              "\"point\":\"search.results\",\"run\":{\"process\":\"persistent\","
              "\"command\":[\"hello.py\"]}}]}");
    assert_ok(RUN(db, "settings", "set", "plugin.hooker.enabled", "true", "--scope", "brain"));
    assert_ok(RUN(db, "add", "body"));
    assert_rows(db, "none");
    free(path);
    free(db);
}

TEST(no_hooks_inserts_nothing_and_hooks_runs_an_action_that_leaves_local_out)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, FILTER("\"origins\":[\"sync\"]")));

    assert_ok(RUN(db, "add", "plain one"));
    assert_rows(db, "none");
    assert_ok(RUN(db, "add", "--no-hooks", "plain two"));
    assert_rows(db, "none");
    assert_ok(RUN(db, "add", "--hooks", "forced"));
    assert_rows(db, "memory.created");
    assert_query(db, "SELECT json_extract(payload, '$.cause.origin') FROM outbox;", "local");
    assert_ok(RUN(db, "update", "--hooks", "3", "--text", "forced again"));
    assert_rows(db, "memory.created,memory.updated");
    assert_ok(RUN(db, "update", "--no-hooks", "3", "--text", "forced thrice"));
    assert_rows(db, "memory.created,memory.updated");
    free(db);
}

TEST(no_hooks_inserts_nothing_on_a_plugin_that_would_take_the_event)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));

    assert_ok(RUN(db, "add", "--no-hooks", "one"));
    assert_rows(db, "none");
    assert_ok(RUN(db, "add", "two"));
    assert_rows(db, "memory.created");
    free(db);
}

TEST(both_hooks_flags_are_a_usage_error_on_every_write_command_and_write_nothing)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
    char *export_path = harness_side_path(db, ".export.json");
    static const char *const k_cmds[][5] = {
        {"add", "x", NULL, NULL, NULL},
        {"update", "1", "--text", "y", NULL},
        {"delete", "1", NULL, NULL, NULL},
        {"rekey", "1", "--to-key", "k:z", NULL},
        {"link", "1", "2", NULL, NULL},
        {"unlink", "1", "2", NULL, NULL},
        {"purge", "--deleted", NULL, NULL, NULL},
        {"import", "--from-json", "/nonexistent.json", NULL, NULL},
        {"conflict", "accept", "--id", "1", "--keep"},
    };
    size_t i = 0;

    assert_ok(RUN(db, "add", "one"));
    assert_ok(RUN(db, "add", "two"));
    assert_ok(RUN(db, "add", "three"));
    for (i = 0; i < sizeof(k_cmds) / sizeof(k_cmds[0]); i++) {
        const char *args[10];
        size_t n = 0;
        size_t k = 0;
        CmdResult r;

        for (k = 0; k < 5U && k_cmds[i][k] != NULL; k++) {
            args[n++] = k_cmds[i][k];
        }
        if (strcmp(k_cmds[i][0], "conflict") == 0) {
            args[n++] = "local";
        }
        args[n++] = "--hooks";
        args[n++] = "--no-hooks";
        r = run_remember(db, args, n, NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, "remember: --hooks and --no-hooks can't be used together\n");
        cmd_result_free(&r);
    }
    assert_query(
        db,
        "SELECT count(*) FROM entries WHERE deleted_at IS NULL AND body IN ('one','two','three');",
        "3");
    assert_rows(db, "memory.created,memory.created,memory.created");
    assert_query(db, "SELECT count(*) FROM entry_links;", "0");
    free(export_path);
    free(db);
}

TEST(one_hooks_flag_on_a_write_command_is_still_accepted_everywhere)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));

    assert_ok(RUN(db, "add", "one", "--hooks"));
    assert_ok(RUN(db, "add", "two", "--no-hooks"));
    assert_ok(RUN(db, "link", "1", "2", "--hooks"));
    assert_ok(RUN(db, "unlink", "1", "2", "--no-hooks"));
    assert_ok(RUN(db, "rekey", "1", "--to-key", "k:a", "--hooks"));
    assert_ok(RUN(db, "delete", "1", "--no-hooks"));
    assert_ok(RUN(db, "purge", "--deleted", "--hooks"));
    free(db);
}

TEST(each_write_command_makes_its_event_with_its_own_command_word)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));

    assert_ok(RUN(db, "add", "one"));
    assert_ok(RUN(db, "add", "two"));
    assert_ok(RUN(db, "update", "1", "--text", "one again"));
    assert_ok(RUN(db, "rekey", "1", "--to-key", "k:one"));
    assert_ok(RUN(db, "link", "1", "2"));
    assert_ok(RUN(db, "unlink", "1", "2"));
    assert_ok(RUN(db, "delete", "1"));
    assert_ok(RUN(db, "purge", "--deleted"));
    assert_query(
        db,
        "SELECT group_concat(json_extract(payload, '$.cause.command') || ':' || type, ' ') "
        "FROM (SELECT payload, type FROM outbox ORDER BY rowid);",
        "add:memory.created add:memory.created update:memory.updated "
        "rekey:memory.updated link:memory.updated link:memory.updated "
        "unlink:memory.updated unlink:memory.updated delete:memory.deleted "
        "purge:memory.purged");
    free(db);
}

TEST(a_read_command_and_a_settings_write_make_no_event_and_arm_nothing)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));

    assert_ok(RUN(db, "add", "one"));
    assert_ok(RUN(db, "list"));
    assert_ok(RUN(db, "get", "1"));
    assert_ok(RUN(db, "search", "one"));
    assert_ok(RUN(db, "tags"));
    assert_ok(RUN(db, "settings", "set", "remember.search.max_results", "5", "--scope", "brain"));
    assert_rows(db, "memory.created");
    free(db);
}

TEST(a_delete_with_expired_hard_wipe_writes_purged_events)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));

    assert_ok(RUN(db, "add", "old", "--expires", "2001-01-01"));
    assert_ok(RUN(db, "purge", "--expired"));
    assert_rows(db, "memory.created,memory.purged");
    free(db);
}

static char *given_export_of_two(void)
{
    char *src = make_temp_db_path();
    char *file = harness_side_path(src, ".export.json");

    assert_ok(RUN(src, "add", "first", "--tag", "red"));
    assert_ok(RUN(src, "add", "second"));
    assert_ok(RUN(src, "export", "--format", "json", "--out", file));
    free(src);
    return file;
}

TEST(default_origins_skip_sync_and_import_and_log_skipped_source)
{
    char *file = given_export_of_two();
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
    char *log = harness_side_path(db, ".d/hooks.log");
    char *text = NULL;
    size_t len = 0;

    assert_ok(RUN(db, "import", "--from-json", file));
    assert_rows(db, "none");
    assert_query(db, "SELECT count(*) FROM entries;", "2");
    text = harness_read_file(log, &len);
    ASSERT_TRUE(text != NULL);
    ASSERT_STR_CONTAINS(text, "\"event\":\"skipped_source\",\"event_type\":\"memory.created\","
                              "\"command\":\"import\",\"origin\":\"import\",\"plugin\":\"hooker\","
                              "\"action\":\"go\",\"count\":2}\n");
    ASSERT_TRUE(text != NULL && len > 0U && strchr(text, '\n') == text + len - 1U);
    free(text);
    free(log);
    free(db);
    free(file);
}

TEST(an_action_that_lists_import_gets_one_event_per_memory_with_one_batch)
{
    char *file = given_export_of_two();
    char *db =
        given_brain_with_enabled_plugin(ACTION(ALL_TYPES, FILTER("\"origins\":[\"import\"]")));
    char *log = harness_side_path(db, ".d/hooks.log");

    assert_ok(RUN(db, "import", "--from-json", file));
    assert_rows(db, "memory.created,memory.created");
    assert_query(db, "SELECT count(DISTINCT json_extract(payload, '$.batch_id')) FROM outbox;",
                 "1");
    assert_query(db,
                 "SELECT group_concat(DISTINCT json_extract(payload, '$.cause.origin') || ':' || "
                 "json_extract(payload, '$.cause.command')) FROM outbox;",
                 "import:import");
    ASSERT_FALSE(access(log, F_OK) == 0);
    free(log);
    free(db);
    free(file);
}

TEST(import_with_hooks_runs_the_default_origins_action_for_that_one_import)
{
    char *file = given_export_of_two();
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
    char *log = harness_side_path(db, ".d/hooks.log");

    assert_ok(RUN(db, "import", "--from-json", file, "--hooks"));
    assert_rows(db, "memory.created,memory.created");
    ASSERT_FALSE(access(log, F_OK) == 0);
    free(log);
    free(db);
    free(file);
}

TEST(import_from_a_database_makes_events_too)
{
    char *src = make_temp_db_path();
    char *db =
        given_brain_with_enabled_plugin(ACTION(ALL_TYPES, FILTER("\"origins\":[\"import\"]")));

    assert_ok(RUN(src, "add", "from the other brain"));
    assert_ok(RUN(db, "import", "--from-db", src));
    assert_rows(db, "memory.created");
    free(src);
    free(db);
}

/* A brain that holds key k:1 with another body than the export, and the export imported: one
   open conflict, no event yet (an import that only opens a conflict changes no memory). */
static char *given_brain_with_a_key_clash(const char *file)
{
    char *db =
        given_brain_with_enabled_plugin(ACTION(ALL_TYPES, FILTER("\"origins\":[\"import\"]")));

    assert_ok(RUN(db, "add", "--key", "k:1", "body from b"));
    assert_rows(db, "none");
    assert_ok(RUN(db, "import", "--from-json", file));
    assert_rows(db, "none");
    return db;
}

static char *given_export_of_key(void)
{
    char *src = make_temp_db_path();
    char *file = harness_side_path(src, ".export.json");

    assert_ok(RUN(src, "add", "--key", "k:1", "body from a"));
    assert_ok(RUN(src, "export", "--format", "json", "--out", file));
    free(src);
    return file;
}

TEST(conflict_accept_makes_the_events_of_what_the_resolution_changed)
{
    char *file = given_export_of_key();
    char *keep_both = given_brain_with_a_key_clash(file);
    char *keep_incoming = given_brain_with_a_key_clash(file);

    assert_ok(RUN(keep_both, "conflict", "accept", "--id", "1", "--keep", "both", "--hooks"));
    assert_query(keep_both,
                 "SELECT group_concat(type || ':' || json_extract(payload, "
                 "'$.cause.command') || ':' || json_extract(payload, '$.memory.body')) "
                 "FROM (SELECT * FROM outbox ORDER BY rowid);",
                 "memory.created:update:body from a");
    assert_ok(
        RUN(keep_incoming, "conflict", "accept", "--id", "1", "--keep", "incoming", "--hooks"));
    assert_query(keep_incoming,
                 "SELECT group_concat(type || ':' || COALESCE(json_extract(payload, "
                 "'$.changes.fields[0]'), '-') || ':' || json_extract(payload, '$.memory.body')) "
                 "FROM (SELECT * FROM outbox ORDER BY rowid);",
                 "memory.updated:key:body from b,memory.created:-:body from a");
    free(file);
    free(keep_both);
    free(keep_incoming);
}

TEST(a_command_that_fails_makes_no_row_logs_nothing_and_warns_of_nothing)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
    CmdResult r;

    assert_ok(RUN(db, "add", "one"));
    r = RUN(db, "add", "");
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = RUN(db, "delete", "99");
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    assert_rows(db, "memory.created");
    free(db);
}

/* user.db exists and is not a database. Restored by given_user_db_back. */
static char *given_unreadable_user_db(void)
{
    const char *xdg = getenv("XDG_DATA_HOME"); // NOLINT(concurrency-mt-unsafe)
    char *path = harness_side_path(xdg != NULL ? xdg : "", "/remember/user.db");
    char *aside = harness_side_path(path, ".aside");

    ASSERT_TRUE(xdg != NULL);
    (void)rename(path, aside);
    harness_write_file(path, "this file is not a sqlite database, and it is long enough that "
                             "sqlite reads a whole header from it and refuses the header.");
    free(aside);
    return path;
}

static void given_user_db_back(char *path)
{
    char *aside = harness_side_path(path, ".aside");

    ASSERT_EQ_INT(unlink(path), 0);
    (void)rename(aside, path);
    free(aside);
    free(path);
}

TEST(an_unreadable_user_db_refuses_a_write_with_the_sqlite_reason)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
    char *user = given_unreadable_user_db();
    CmdResult r = RUN(db, "add", "kept out");

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: sqlite: file is not a database\n");
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries;", "0");
    given_user_db_back(user);
    free(db);
}

/* The temp triggers write rows of their own: a change count that includes them saw a tag
   added on every repeated add, and bumped the version vector (sync then sees a new version). */
TEST(an_add_that_merges_nothing_new_leaves_the_memory_as_it_was_while_events_are_on)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
    const char *state = "SELECT change_seq || ' ' || version_vector || ' ' || updated_at "
                        "FROM entries WHERE key = 'k';";
    char *first = NULL;
    char *again = NULL;

    assert_ok(RUN(db, "add", "--key", "k", "--tag", "t", "body"));
    first = harness_sqlite_query_line(db, state);
    assert_ok(RUN(db, "add", "--key", "k", "--tag", "t", "body"));
    assert_ok(RUN(db, "add", "--no-hooks", "--key", "k", "--tag", "t", "body"));
    again = harness_sqlite_query_line(db, state);
    ASSERT_TRUE(first != NULL && again != NULL);
    ASSERT_STREQ(again != NULL ? again : "", first != NULL ? first : "-");
    assert_rows(db, "memory.created");
    free(first);
    free(again);
    free(db);
}

TEST(processes_off_warns_plugin_unsupported_c23)
{
    char *db = given_brain_with_enabled_plugin(ACTION(CREATED_ONLY, ""));
    char *out = NULL;
    char *err = NULL;
    size_t out_len = 0;
    size_t err_len = 0;
    FILE *fout = NULL;
    FILE *ferr = NULL;
    const char *argv[] = {"remember", "--db", db, "add", "saved anyway"};
    const char *strict[] = {"remember", "--strict", "--db", db, "add", "saved too"};
    const char *json[] = {"remember", "--json", "--db", db, "add", "saved as json"};
    const char *other[] = {"remember", "--db", db, "delete", "1"};
    int rc = 0;

    events_set_processes(false);
    fout = open_memstream(&out, &out_len);
    ferr = open_memstream(&err, &err_len);
    rc = remember_run(5, (char *const *)argv, fout, ferr);
    (void)fclose(fout);
    (void)fclose(ferr);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(err, "remember: warning: the write was saved, but this build of remember can't "
                      "start programs, so its hooks and plugins didn't run. Use a build of "
                      "remember that can start programs.\n");
    free(out);
    free(err);
    out = NULL;
    err = NULL;
    fout = open_memstream(&out, &out_len);
    ferr = open_memstream(&err, &err_len);
    rc = remember_run(6, (char *const *)strict, fout, ferr);
    (void)fclose(fout);
    (void)fclose(ferr);
    ASSERT_EQ_INT(rc, 4);
    ASSERT_STR_CONTAINS(err, "remember: warning: the write was saved, but this build of remember");
    free(out);
    free(err);
    out = NULL;
    err = NULL;
    fout = open_memstream(&out, &out_len);
    ferr = open_memstream(&err, &err_len);
    rc = remember_run(6, (char *const *)json, fout, ferr);
    (void)fclose(fout);
    (void)fclose(ferr);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STR_CONTAINS(out, "\"warnings\":[{\"code\":\"plugin_unsupported\",\"message\":\"the "
                             "write was saved, but this build of remember can't start programs, so "
                             "its hooks and plugins didn't run.\",\"fix\":\"Use a build of "
                             "remember that can start programs.\"}]");
    ASSERT_STREQ(err, "");
    free(out);
    free(err);
    out = NULL;
    err = NULL;
    /* an event no action lists is no match: no warning */
    fout = open_memstream(&out, &out_len);
    ferr = open_memstream(&err, &err_len);
    rc = remember_run(5, (char *const *)other, fout, ferr);
    (void)fclose(fout);
    (void)fclose(ferr);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(err, "");
    free(out);
    free(err);
    events_set_processes(true);
    assert_rows(db, "none");
    assert_query(db, "SELECT count(*) FROM entries;", "3");
    free(db);
}

TEST(a_write_in_a_run_gets_its_cause_from_the_environment)
{
    char *db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));

    given_run_env("[]");
    assert_ok(RUN(db, "add", "from a hook"));
    clear_cause_env();
    assert_rows(db, "memory.created");
    assert_query(db,
                 "SELECT json_extract(payload, '$.cause.hook') || ':' || "
                 "json_extract(payload, '$.cause.run_id') || ':' || "
                 "json_extract(payload, '$.cause.depth') || ':' || "
                 "json_extract(payload, '$.cause.chain[0].hook') || ':' || "
                 "json_extract(payload, '$.cause.chain[0].memory_id') FROM outbox;",
                 "plugin/url_expand/fetch:01a14521-0bf0-7000-8000-0000000000c1:2:"
                 "plugin/url_expand/fetch:01a14521-09ff-7d20-8c00-00000000aa01");
    free(db);
}

/* The brain path an event shows when HOME is home (as typed: a trailing / included). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the HOME suffix, then nothing else
static void assert_tilde_with_home_suffix(const char *suffix)
{
    char *db = given_brain_with_enabled_plugin(ACTION(CREATED_ONLY, ""));
    char *dir = dir_of_path(db);
    char *home = harness_side_path(dir, suffix);
    char *saved_home = NULL;
    const char *old = getenv("HOME"); // NOLINT(concurrency-mt-unsafe)
    char want[512];

    saved_home = old != NULL ? strdup(old) : NULL;
    ASSERT_EQ_INT(setenv("HOME", home, 1), 0); // NOLINT(concurrency-mt-unsafe)
    assert_ok(RUN(db, "add", "one"));
    if (saved_home != NULL) {
        ASSERT_EQ_INT(setenv("HOME", saved_home, 1), 0); // NOLINT(concurrency-mt-unsafe)
    }
    (void)snprintf(want, sizeof(want), "~%s", db + strlen(dir));
    assert_query(db, "SELECT json_extract(payload, '$.brain.path') FROM outbox;", want);
    free(saved_home);
    free(home);
    free(dir);
    free(db);
}

TEST(the_brain_path_in_an_event_is_absolute_and_a_home_prefix_is_a_tilde)
{
    assert_tilde_with_home_suffix("");
}

TEST(a_home_with_a_trailing_slash_still_gives_the_tilde)
{
    assert_tilde_with_home_suffix("/");
}

#ifdef REMEMBER_TEST_VFS
TEST(a_failed_write_commits_neither_the_memory_nor_its_outbox_row)
{
    bool saw_failure = false;
    bool saw_success = false;

    /* The brain's first write needs about 50 file writes: sweep the end of them, and the start. */
    for (int at = 0; at < 70; at++) {
        char *db = NULL;
        char count[16];
        CmdResult r;
        char *got = NULL;

        if (at > 1 && at < 36) {
            continue;
        }
        db = given_brain_with_enabled_plugin(ACTION(ALL_TYPES, ""));
        (void)snprintf(count, sizeof(count), "%d", at);
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_WRITE", count, 1), 0);
        r = RUN(db, "add", "body");
        ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_WRITE"), 0); // NOLINT(concurrency-mt-unsafe)
        got = harness_sqlite_query_line(db, "SELECT (SELECT count(*) FROM entries) || '/' || "
                                            "(SELECT count(*) FROM outbox);");
        ASSERT_TRUE(got != NULL);
        if (got != NULL && strcmp(got, "1/1") == 0) {
            saw_success = true;
            ASSERT_EQ_INT(r.exit_code, 0);
        } else if (got != NULL && strcmp(got, "0/0") == 0) {
            saw_failure = true;
            ASSERT_TRUE(r.exit_code != 0);
        } else {
            ASSERT_STREQ(got != NULL ? got : "", "1/1 or 0/0");
        }
        free(got);
        cmd_result_free(&r);
        free(db);
    }
    ASSERT_TRUE(saw_failure);
    ASSERT_TRUE(saw_success);
}
#endif

#endif /* REMEMBER_PLUGINS_DIAG_TEST */

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

void register_events_tests(void)
{
    RUN_TEST(event_type_and_origin_names);
    RUN_TEST(plan_a_new_row_is_one_created_event);
    RUN_TEST(plan_a_new_deleted_row_is_still_created);
    RUN_TEST(plan_a_removed_row_is_one_purged_event);
    RUN_TEST(plan_a_row_that_came_and_went_in_one_write_is_no_event);
    RUN_TEST(plan_deleted_at_set_is_deleted_and_cleared_is_restored);
    RUN_TEST(plan_a_delete_that_also_changes_fields_is_only_deleted);
    RUN_TEST(plan_each_field_change_is_one_updated_with_only_its_flag);
    RUN_TEST(plan_a_tag_replaced_by_another_of_the_same_count_is_a_tags_change);
    RUN_TEST(plan_a_key_or_expiry_cleared_is_a_change);
    RUN_TEST(plan_nothing_the_event_list_names_changed_is_no_event);
    RUN_TEST(plan_a_link_that_came_and_one_that_went_are_one_updated_each);
    RUN_TEST(plan_the_same_other_memory_under_another_kind_or_direction_is_another_link);
    RUN_TEST(plan_fields_and_a_link_are_separate_events_fields_first);
    RUN_TEST(plan_a_link_of_a_new_or_removed_row_adds_no_event);
    RUN_TEST(rules_hold_the_actions_and_not_the_filters_of_a_plugin);
    RUN_TEST(an_action_with_no_filter_matches_a_local_event_of_its_type_only);
    RUN_TEST(memory_star_lists_every_type_and_a_list_each_listed_one);
    RUN_TEST(tags_any_needs_one_pattern_to_match_a_tag);
    RUN_TEST(tags_all_needs_every_pattern_to_match_some_tag);
    RUN_TEST(any_and_all_must_both_pass_and_an_empty_list_is_no_test);
    RUN_TEST(a_tag_pattern_ending_in_star_is_a_prefix);
    RUN_TEST(match_before_lets_the_tags_before_the_write_pass_too);
    RUN_TEST(created_by_and_updated_by_are_tested_by_actor_kind);
    RUN_TEST(bins_tests_the_bin_the_memory_had_before_the_write);
    RUN_TEST(not_caused_by_skips_an_event_a_named_hook_or_plugin_caused);
    RUN_TEST(origins_default_to_local_and_an_empty_list_matches_none);
    RUN_TEST(hooks_on_ignores_origins_but_not_the_rest_of_the_filter);
    RUN_TEST(a_miss_of_another_filter_is_a_filter_miss_even_when_origins_miss_too);
    RUN_TEST(a_miss_is_counted_on_the_action_by_its_kind);
    RUN_TEST(a_created_payload_is_the_frozen_h3_data_without_the_recipient_fields);
    RUN_TEST(deleted_restored_and_purged_payloads_carry_null_changes);
    RUN_TEST(an_updated_payload_names_the_changed_fields_and_their_old_values);
    RUN_TEST(an_updated_payload_for_one_field_lists_only_that_field);
    RUN_TEST(a_link_payload_carries_changes_link_and_the_link_stubs_have_no_local_id);
    RUN_TEST(a_payload_written_in_a_run_names_the_hook_run_depth_and_chain);
    RUN_TEST(the_origin_and_command_of_the_stamp_are_written);
    RUN_TEST(a_payload_under_the_limit_is_not_cut);
    RUN_TEST(over_the_limit_the_old_body_goes_first_and_nothing_else_when_that_is_enough);
    RUN_TEST(the_old_body_stays_in_a_changes_that_has_other_fields_and_loses_only_the_body);
    RUN_TEST(over_the_limit_with_no_old_body_the_links_go_next);
    RUN_TEST(truncation_order_256k);
    RUN_TEST(the_body_is_cut_on_a_utf8_boundary);
    RUN_TEST(a_big_old_body_is_cut_when_the_new_body_is_short);
    RUN_TEST(a_body_of_exactly_the_cut_length_is_not_cut_even_in_a_big_payload);
    RUN_TEST(recipients_json_lists_each_entry_pending);
    RUN_TEST(the_processes_switch_defaults_on_and_follows_the_setter);
    RUN_TEST(a_write_outside_a_run_is_a_direct_cause);
    RUN_TEST(a_run_with_no_chain_variable_has_the_hook_run_and_depth_and_an_empty_chain);
    RUN_TEST(a_valid_chain_gets_this_runs_hop_appended);
    RUN_TEST(an_empty_chain_variable_gives_a_chain_of_just_this_hop);
    RUN_TEST(a_chain_that_is_not_valid_is_dropped_and_the_write_counts_as_depth_one_of_its_own);
    RUN_TEST(ninety_nine_hops_and_this_one_fill_the_chain);
    RUN_TEST(a_missing_or_bad_hop_variable_drops_the_chain);
    RUN_TEST(a_bad_hook_name_or_run_id_reads_as_none_and_the_depth_stays);
    RUN_TEST(a_depth_that_is_not_a_whole_number_of_one_to_a_million_is_not_a_run);
    RUN_TEST(reading_the_cause_fails_when_memory_runs_out);
    RUN_TEST(planning_a_link_fails_cleanly_when_memory_runs_out);
    RUN_TEST(ten_actions_of_one_plugin_are_ten_rules);
    RUN_TEST(planning_ten_new_links_keeps_ten_drafts);
    RUN_TEST(loading_the_rules_reads_enabled_and_reports_why_it_failed);
    RUN_TEST(skipped_source_lines_are_one_per_action_and_type_with_the_count);
    RUN_TEST(a_local_write_logs_no_skipped_source_line_and_keeps_its_counts);
    RUN_TEST(nothing_counted_makes_no_log_file);
    RUN_TEST(the_log_is_mode_0600_and_a_log_that_cannot_be_written_is_not_an_error);
    RUN_TEST(a_fifo_at_hooks_log_neither_hangs_the_command_nor_takes_a_line);
    RUN_TEST(six_event_types_created);
    RUN_TEST(a_link_writes_two_updated_events_with_one_batch_and_no_vector_change);
    RUN_TEST(a_link_to_a_memory_that_is_purged_updates_the_neighbour_too);
    RUN_TEST(no_enabled_action_no_row);
    RUN_TEST(the_flush_finds_each_memory_s_old_tags_and_links_by_an_index);
    RUN_TEST(an_event_with_no_matching_action_is_not_even_built);
    RUN_TEST(filter_miss_inserts_nothing_counts_skipped);
    RUN_TEST(created_by_is_the_creator_even_after_another_kind_writes);
    RUN_TEST(each_action_gets_the_row_it_matches_and_the_recipients_list_only_those);
    RUN_TEST(default_origins_skip_sync_and_import_and_count_skipped_source);
    RUN_TEST(an_action_that_lists_the_origin_gets_the_event_with_that_origin);
    RUN_TEST(hooks_flag_ignores_origins);
    RUN_TEST(no_hooks_inserts_nothing);
    RUN_TEST(hooks_on_does_not_ignore_a_filter_other_than_origins);
    RUN_TEST(with_processes_off_a_matching_write_inserts_no_row_and_is_counted);
    RUN_TEST(an_unchanged_write_makes_no_event);
    RUN_TEST(a_tag_change_alone_is_one_updated_event_with_the_tags_difference);
    RUN_TEST(a_failed_write_leaves_no_trace_in_the_next_one);
    RUN_TEST(a_whole_bin_purge_writes_one_purged_event_per_row_with_its_last_state);
    RUN_TEST(the_row_and_its_payload_have_the_shape_plan_35_reads);
    RUN_TEST(no_body_or_tag_text_is_in_a_column_a_dispatcher_puts_in_argv_or_env);
    RUN_TEST(one_command_shares_one_batch_id_and_a_new_command_has_its_own);
    RUN_TEST(a_write_inside_a_run_carries_the_hook_cause_and_not_caused_by_skips_it);
    RUN_TEST(bins_and_match_before_read_the_state_before_the_write);
    RUN_TEST(outbox_commits_with_memory);
    RUN_TEST(an_update_of_a_tagged_linked_memory_commits_with_its_row_or_not_at_all);
#ifdef REMEMBER_PLUGINS_DIAG_TEST
    RUN_TEST(a_plugin_gets_rows_only_once_it_is_enabled_for_the_brain);
    RUN_TEST(a_plugin_enabled_in_one_brain_does_not_give_rows_in_another);
    RUN_TEST(a_plugin_with_only_search_filters_gets_no_rows);
    RUN_TEST(no_hooks_inserts_nothing_and_hooks_runs_an_action_that_leaves_local_out);
    RUN_TEST(no_hooks_inserts_nothing_on_a_plugin_that_would_take_the_event);
    RUN_TEST(both_hooks_flags_are_a_usage_error_on_every_write_command_and_write_nothing);
    RUN_TEST(one_hooks_flag_on_a_write_command_is_still_accepted_everywhere);
    RUN_TEST(each_write_command_makes_its_event_with_its_own_command_word);
    RUN_TEST(a_read_command_and_a_settings_write_make_no_event_and_arm_nothing);
    RUN_TEST(a_delete_with_expired_hard_wipe_writes_purged_events);
    RUN_TEST(default_origins_skip_sync_and_import_and_log_skipped_source);
    RUN_TEST(an_action_that_lists_import_gets_one_event_per_memory_with_one_batch);
    RUN_TEST(import_with_hooks_runs_the_default_origins_action_for_that_one_import);
    RUN_TEST(import_from_a_database_makes_events_too);
    RUN_TEST(conflict_accept_makes_the_events_of_what_the_resolution_changed);
    RUN_TEST(a_command_that_fails_makes_no_row_logs_nothing_and_warns_of_nothing);
    RUN_TEST(an_unreadable_user_db_refuses_a_write_with_the_sqlite_reason);
    RUN_TEST(an_add_that_merges_nothing_new_leaves_the_memory_as_it_was_while_events_are_on);
    RUN_TEST(processes_off_warns_plugin_unsupported_c23);
    RUN_TEST(a_write_in_a_run_gets_its_cause_from_the_environment);
    RUN_TEST(the_brain_path_in_an_event_is_absolute_and_a_home_prefix_is_a_tilde);
    RUN_TEST(a_home_with_a_trailing_slash_still_gives_the_tilde);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(a_failed_write_commits_neither_the_memory_nor_its_outbox_row);
#endif
#endif
}
