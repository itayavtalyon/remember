#include "outbox.h"

#include <stddef.h>
#include <stdint.h>

/* A terminal state keeps no backoff and no retry time; the caller never runs it
   again by itself. */
static OutboxStep step_to(OutboxState state)
{
    return (OutboxStep){.state = state, .retry_at = OUTBOX_RETRY_CLEAR};
}

static OutboxStep step_failed_attempt(int64_t waited_s)
{
    int64_t next = 0;

    if (waited_s >= OUTBOX_BACKOFF_CAP_S) {
        return step_to(OUTBOX_FAILED);
    }
    /* waited_s is below the cap here, so the doubling cannot overflow however
       large a hand-edited row's backoff_s is. */
    next = (waited_s == 0) ? OUTBOX_BACKOFF_FIRST_S : waited_s * 2;
    return (OutboxStep){.state = OUTBOX_PENDING,
                        .backoff_s = (next > OUTBOX_BACKOFF_CAP_S) ? OUTBOX_BACKOFF_CAP_S : next,
                        .retry_at = OUTBOX_RETRY_AFTER_BACKOFF};
}

static OutboxStep step_not_reported(const OutboxInput *in)
{
    if (in->state != OUTBOX_PENDING) {
        return step_to(in->state);
    }
    if (in->chain_len >= OUTBOX_HALT_CHAIN) {
        return step_to(OUTBOX_HALTED);
    }
    return (OutboxStep){.state = OUTBOX_PENDING, .run_now = true, .backoff_s = in->waited_s};
}

static OutboxStep step_reported(const OutboxInput *in)
{
    if (in->result == OUTBOX_RESULT_PERSISTENT_ACK ||
        (in->result == OUTBOX_RESULT_ONCE_EXIT && in->exit_code == 0)) {
        return step_to(OUTBOX_DONE);
    }
    if (in->result == OUTBOX_RESULT_PREFLIGHT) {
        return step_to(OUTBOX_FAILED);
    }
    return step_failed_attempt(in->waited_s);
}

static bool input_ok(const OutboxInput *in)
{
    return (unsigned)in->state <= (unsigned)OUTBOX_HALTED &&
           (unsigned)in->result <= (unsigned)OUTBOX_RESULT_PREFLIGHT && in->chain_len >= 0 &&
           in->age_days >= 0 && in->waited_s >= 0;
}

bool outbox_step(const OutboxInput *in, OutboxStep *out)
{
    OutboxStep next;

    if (in == NULL || out == NULL || !input_ok(in)) {
        return false;
    }
    if (in->state == OUTBOX_DONE || in->event_done) {
        next = step_to(OUTBOX_DONE);
    } else if (in->result == OUTBOX_RESULT_NONE) {
        next = step_not_reported(in);
    } else {
        next = step_reported(in);
    }
    if ((next.state == OUTBOX_FAILED || next.state == OUTBOX_HALTED) &&
        in->age_days >= OUTBOX_DROP_DAYS) {
        next.delete_row = true;
    }
    *out = next;
    return true;
}

size_t outbox_lane_count(void)
{
    return OUTBOX_LANES;
}

bool outbox_lane_allows(size_t lane)
{
    return lane < OUTBOX_LANES;
}
