#include "outbox.h"
#include "register.h"
#include "test.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Pure unit tests for outbox_step (plan 27a, X3 A, E1 A, Q7 A). No CLI, no
 * store, no clock. Expected values are literals, not the formula under test.
 */

/* Expected values are literals on purpose (standard:coding PROOF): a named
   constant would copy the rule under test. */
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

static OutboxInput given_pending(void)
{
    return (OutboxInput){.state = OUTBOX_PENDING, .result = OUTBOX_RESULT_NONE};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the result, then the delay waited
static OutboxInput given_attempt(OutboxResult result, int64_t waited_s)
{
    OutboxInput in = given_pending();

    in.result = result;
    in.waited_s = waited_s;
    return in;
}

static OutboxStep when_step(OutboxInput in)
{
    OutboxStep out = {.backoff_s = -1};

    ASSERT_TRUE(outbox_step(&in, &out));
    return out;
}

TEST(outbox_crash_before_a_result_stays_pending_and_runs_again)
{
    OutboxStep out = when_step(given_pending());

    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_TRUE(out.run_now);
    ASSERT_EQ_INT(out.backoff_s, 0);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_KEEP);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_an_event_id_already_done_is_not_run)
{
    OutboxInput in = given_pending();
    OutboxStep out;

    in.event_done = true;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
    ASSERT_FALSE(out.run_now);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_a_done_recipient_stays_done_and_is_not_run)
{
    OutboxInput in = given_pending();
    OutboxStep out;

    in.state = OUTBOX_DONE;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
    ASSERT_FALSE(out.run_now);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_once_process_exit_0_sets_done)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 0);
    OutboxStep out;

    in.exit_code = 0;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
    ASSERT_FALSE(out.run_now);
    ASSERT_EQ_INT(out.backoff_s, 0);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_CLEAR);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_a_matching_event_result_sets_done)
{
    OutboxStep out = when_step(given_attempt(OUTBOX_RESULT_PERSISTENT_ACK, 8));

    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
    ASSERT_FALSE(out.run_now);
    ASSERT_EQ_INT(out.backoff_s, 0);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_CLEAR);
}

TEST(outbox_first_failed_attempt_stays_pending_delay_1s)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 0);
    OutboxStep out;

    in.exit_code = 1;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_EQ_INT(out.backoff_s, 1);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_AFTER_BACKOFF);
    ASSERT_FALSE(out.run_now);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_a_persistent_run_with_no_event_result_is_a_failed_attempt)
{
    OutboxStep out = when_step(given_attempt(OUTBOX_RESULT_PERSISTENT_NONE, 0));

    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_EQ_INT(out.backoff_s, 1);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_AFTER_BACKOFF);
    ASSERT_FALSE(out.run_now);
}

TEST(outbox_exit_75_is_a_failed_attempt_like_any_other_non_zero_exit)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 4);
    OutboxStep out;

    in.exit_code = 75;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_EQ_INT(out.backoff_s, 8);
}

TEST(outbox_backoff_doubles_then_caps_at_3600s)
{
    static const int64_t want[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 3600};
    int64_t waited = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, waited);
        OutboxStep out;

        in.exit_code = 1;
        out = when_step(in);
        ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
        ASSERT_EQ_INT(out.backoff_s, want[i]);
        waited = out.backoff_s;
    }
}

TEST(outbox_a_delay_between_2048_and_3600_is_capped_not_doubled)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 3599);
    OutboxStep out;

    in.exit_code = 1;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_EQ_INT(out.backoff_s, 3600);
}

TEST(outbox_failure_after_a_3600s_delay_sets_failed)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 3600);
    OutboxStep out;

    in.exit_code = 1;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_FALSE(out.run_now);
    ASSERT_EQ_INT(out.backoff_s, 0);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_CLEAR);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_preflight_failure_sets_failed_at_once_on_a_first_attempt)
{
    OutboxStep out = when_step(given_attempt(OUTBOX_RESULT_PREFLIGHT, 0));

    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_FALSE(out.run_now);
    ASSERT_EQ_INT(out.backoff_s, 0);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_CLEAR);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_preflight_failure_sets_failed_at_once_after_a_backoff_step)
{
    OutboxStep out = when_step(given_attempt(OUTBOX_RESULT_PREFLIGHT, 64));

    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_FALSE(out.run_now);
    ASSERT_EQ_INT(out.backoff_s, 0);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_CLEAR);
}

TEST(outbox_preflight_failure_is_not_a_backoff_step)
{
    OutboxInput flagged = given_attempt(OUTBOX_RESULT_PREFLIGHT, 0);
    OutboxInput unflagged = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 0);
    OutboxStep out;

    flagged.exit_code = 1;
    unflagged.exit_code = 1;

    out = when_step(flagged);
    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_EQ_INT(out.backoff_s, 0);
    out = when_step(unflagged);
    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_EQ_INT(out.backoff_s, 1);
}

TEST(outbox_crash_does_not_consume_backoff)
{
    OutboxInput in = given_pending();
    OutboxStep out;

    in.waited_s = 32;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_EQ_INT(out.backoff_s, 32);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_KEEP);
    ASSERT_TRUE(out.run_now);
}

TEST(outbox_chain_length_100_sets_halted_and_does_not_run)
{
    OutboxInput in = given_pending();
    OutboxStep out;

    in.chain_len = 100;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_HALTED);
    ASSERT_FALSE(out.run_now);
    ASSERT_EQ_INT(out.backoff_s, 0);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_CLEAR);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_chain_length_99_still_runs)
{
    OutboxInput in = given_pending();
    OutboxStep out;

    in.chain_len = 99;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_PENDING);
    ASSERT_TRUE(out.run_now);
}

TEST(outbox_a_huge_chain_length_sets_halted)
{
    OutboxInput in = given_pending();
    OutboxStep out;

    in.chain_len = INT64_MAX;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_HALTED);
}

TEST(outbox_a_run_that_ended_is_judged_by_its_result_not_the_chain_length)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 0);
    OutboxStep out;

    in.chain_len = 100;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the state, then the age
static OutboxInput given_row(OutboxState state, int64_t age_days)
{
    OutboxInput in = given_pending();

    in.state = state;
    in.age_days = age_days;
    return in;
}

TEST(outbox_a_failed_row_at_6_days_stays)
{
    OutboxStep out = when_step(given_row(OUTBOX_FAILED, 6));

    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_FALSE(out.run_now);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_a_failed_row_at_7_days_is_deleted)
{
    OutboxStep out = when_step(given_row(OUTBOX_FAILED, 7));

    ASSERT_TRUE(out.delete_row);
    ASSERT_FALSE(out.run_now);
}

TEST(outbox_a_halted_row_at_6_days_stays_and_at_7_days_is_deleted)
{
    ASSERT_FALSE(when_step(given_row(OUTBOX_HALTED, 6)).delete_row);
    ASSERT_TRUE(when_step(given_row(OUTBOX_HALTED, 7)).delete_row);
}

TEST(outbox_a_failed_row_of_any_older_age_is_deleted)
{
    ASSERT_TRUE(when_step(given_row(OUTBOX_FAILED, 8)).delete_row);
    ASSERT_TRUE(when_step(given_row(OUTBOX_FAILED, INT64_MAX)).delete_row);
}

TEST(outbox_a_failed_row_stays_failed_and_is_not_run_when_nothing_is_reported)
{
    OutboxInput in = given_row(OUTBOX_FAILED, 0);
    OutboxStep out;

    in.chain_len = 100;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_FALSE(out.run_now);
    ASSERT_EQ_INT(out.retry_at, OUTBOX_RETRY_CLEAR);
}

TEST(outbox_a_halted_row_stays_halted_and_is_not_run_when_nothing_is_reported)
{
    OutboxStep out = when_step(given_row(OUTBOX_HALTED, 0));

    ASSERT_EQ_INT(out.state, OUTBOX_HALTED);
    ASSERT_FALSE(out.run_now);
}

TEST(outbox_a_pending_or_done_row_is_never_deleted_by_age)
{
    OutboxInput done = given_row(OUTBOX_DONE, INT64_MAX);
    OutboxInput pending = given_row(OUTBOX_PENDING, INT64_MAX);

    ASSERT_FALSE(when_step(done).delete_row);
    ASSERT_FALSE(when_step(pending).delete_row);
}

TEST(outbox_a_row_that_fails_for_good_at_7_days_is_deleted)
{
    OutboxInput in = given_row(OUTBOX_PENDING, 7);
    OutboxStep out;

    in.result = OUTBOX_RESULT_PREFLIGHT;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_TRUE(out.delete_row);
}

TEST(outbox_a_rerun_that_succeeds_sets_done_and_keeps_the_row)
{
    OutboxInput in = given_row(OUTBOX_FAILED, 6);
    OutboxStep out;

    in.result = OUTBOX_RESULT_ONCE_EXIT;
    out = when_step(in);

    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
    ASSERT_FALSE(out.delete_row);
}

TEST(outbox_done_is_never_undone_by_a_late_failure_or_halt)
{
    OutboxInput late = given_attempt(OUTBOX_RESULT_PREFLIGHT, 0);
    OutboxInput twin = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 0);
    OutboxStep out;

    late.state = OUTBOX_DONE;
    late.chain_len = 100;
    twin.exit_code = 1;
    twin.event_done = true;

    out = when_step(late);
    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
    ASSERT_FALSE(out.run_now);
    out = when_step(twin);
    ASSERT_EQ_INT(out.state, OUTBOX_DONE);
    ASSERT_FALSE(out.run_now);
}

TEST(outbox_a_delay_past_the_cap_sets_failed_without_overflow)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, INT64_MAX);
    OutboxStep out;

    in.exit_code = 1;
    out = when_step(in);
    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
    ASSERT_EQ_INT(out.backoff_s, 0);

    in.waited_s = 3601;
    out = when_step(in);
    ASSERT_EQ_INT(out.state, OUTBOX_FAILED);
}

TEST(outbox_a_delay_off_the_sequence_still_doubles_and_caps)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 5);

    in.exit_code = 1;
    ASSERT_EQ_INT(when_step(in).backoff_s, 10);
    in.waited_s = 1801;
    ASSERT_EQ_INT(when_step(in).backoff_s, 3600);
    in.waited_s = 1800;
    ASSERT_EQ_INT(when_step(in).backoff_s, 3600);
    in.waited_s = 1799;
    ASSERT_EQ_INT(when_step(in).backoff_s, 3598);
}

TEST(outbox_an_exit_code_of_any_non_zero_value_is_a_failed_attempt)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_ONCE_EXIT, 0);

    in.exit_code = -1;
    ASSERT_EQ_INT(when_step(in).state, OUTBOX_PENDING);
    in.exit_code = 256;
    ASSERT_EQ_INT(when_step(in).state, OUTBOX_PENDING);
}

TEST(outbox_the_exit_code_is_ignored_for_every_other_result)
{
    OutboxInput in = given_attempt(OUTBOX_RESULT_PERSISTENT_NONE, 0);

    in.exit_code = 0;
    ASSERT_EQ_INT(when_step(in).state, OUTBOX_PENDING);
    in.result = OUTBOX_RESULT_PERSISTENT_ACK;
    in.exit_code = 1;
    ASSERT_EQ_INT(when_step(in).state, OUTBOX_DONE);
}

enum { UNTOUCHED = 77 }; /* sentinel: a refused call must not overwrite it */

static void assert_refused(OutboxInput in)
{
    OutboxStep out = {.backoff_s = UNTOUCHED};

    ASSERT_FALSE(outbox_step(&in, &out));
    ASSERT_EQ_INT(out.backoff_s, UNTOUCHED);
}

TEST(outbox_step_refuses_input_it_cannot_judge_and_leaves_out_untouched)
{
    OutboxInput in = given_pending();
    OutboxStep out = {.backoff_s = UNTOUCHED};

    ASSERT_FALSE(outbox_step(NULL, &out));
    ASSERT_FALSE(outbox_step(&in, NULL));

    in = given_pending();
    in.chain_len = -1;
    assert_refused(in);
    in = given_pending();
    in.age_days = -1;
    assert_refused(in);
    in = given_pending();
    in.waited_s = -1;
    assert_refused(in);
    in = given_pending();
    in.waited_s = INT64_MIN;
    assert_refused(in);
    in = given_pending();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the guard under test
    in.state = (OutboxState)4;
    assert_refused(in);
    in = given_pending();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the guard under test
    in.state = (OutboxState)-1;
    assert_refused(in);
    in = given_pending();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the guard under test
    in.result = (OutboxResult)5;
    assert_refused(in);
    in = given_pending();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the guard under test
    in.result = (OutboxResult)-1;
    assert_refused(in);
}

TEST(outbox_step_refuses_bad_input_even_when_the_event_is_already_done)
{
    OutboxInput in = given_pending();

    in.event_done = true;
    in.waited_s = -5;
    assert_refused(in);
}

TEST(outbox_lane_count_is_4_and_a_5th_lane_is_refused)
{
    ASSERT_EQ_INT(outbox_lane_count(), 4);
    ASSERT_TRUE(outbox_lane_allows(0));
    ASSERT_TRUE(outbox_lane_allows(3));
    ASSERT_FALSE(outbox_lane_allows(4));
    ASSERT_FALSE(outbox_lane_allows(SIZE_MAX));
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

void register_outbox_tests(void)
{
    RUN_TEST(outbox_an_event_id_already_done_is_not_run);
    RUN_TEST(outbox_a_done_recipient_stays_done_and_is_not_run);
    RUN_TEST(outbox_once_process_exit_0_sets_done);
    RUN_TEST(outbox_a_matching_event_result_sets_done);
    RUN_TEST(outbox_first_failed_attempt_stays_pending_delay_1s);
    RUN_TEST(outbox_a_persistent_run_with_no_event_result_is_a_failed_attempt);
    RUN_TEST(outbox_exit_75_is_a_failed_attempt_like_any_other_non_zero_exit);
    RUN_TEST(outbox_backoff_doubles_then_caps_at_3600s);
    RUN_TEST(outbox_a_delay_between_2048_and_3600_is_capped_not_doubled);
    RUN_TEST(outbox_failure_after_a_3600s_delay_sets_failed);
    RUN_TEST(outbox_preflight_failure_sets_failed_at_once_on_a_first_attempt);
    RUN_TEST(outbox_preflight_failure_sets_failed_at_once_after_a_backoff_step);
    RUN_TEST(outbox_preflight_failure_is_not_a_backoff_step);
    RUN_TEST(outbox_crash_does_not_consume_backoff);
    RUN_TEST(outbox_chain_length_100_sets_halted_and_does_not_run);
    RUN_TEST(outbox_chain_length_99_still_runs);
    RUN_TEST(outbox_a_huge_chain_length_sets_halted);
    RUN_TEST(outbox_a_run_that_ended_is_judged_by_its_result_not_the_chain_length);
    RUN_TEST(outbox_a_failed_row_at_6_days_stays);
    RUN_TEST(outbox_a_failed_row_at_7_days_is_deleted);
    RUN_TEST(outbox_a_halted_row_at_6_days_stays_and_at_7_days_is_deleted);
    RUN_TEST(outbox_a_failed_row_of_any_older_age_is_deleted);
    RUN_TEST(outbox_a_failed_row_stays_failed_and_is_not_run_when_nothing_is_reported);
    RUN_TEST(outbox_a_halted_row_stays_halted_and_is_not_run_when_nothing_is_reported);
    RUN_TEST(outbox_a_pending_or_done_row_is_never_deleted_by_age);
    RUN_TEST(outbox_a_row_that_fails_for_good_at_7_days_is_deleted);
    RUN_TEST(outbox_a_rerun_that_succeeds_sets_done_and_keeps_the_row);
    RUN_TEST(outbox_done_is_never_undone_by_a_late_failure_or_halt);
    RUN_TEST(outbox_a_delay_past_the_cap_sets_failed_without_overflow);
    RUN_TEST(outbox_a_delay_off_the_sequence_still_doubles_and_caps);
    RUN_TEST(outbox_an_exit_code_of_any_non_zero_value_is_a_failed_attempt);
    RUN_TEST(outbox_the_exit_code_is_ignored_for_every_other_result);
    RUN_TEST(outbox_step_refuses_input_it_cannot_judge_and_leaves_out_untouched);
    RUN_TEST(outbox_step_refuses_bad_input_even_when_the_event_is_already_done);
    RUN_TEST(outbox_lane_count_is_4_and_a_5th_lane_is_refused);
    RUN_TEST(outbox_crash_before_a_result_stays_pending_and_runs_again);
}
