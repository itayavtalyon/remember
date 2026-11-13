#ifndef REMEMBER_OUTBOX_H
#define REMEMBER_OUTBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Outbox delivery (plan 27a; specs/OUTBOX-DELIVERY.md). Pure state machine:
 * one function decides the next state of one recipient. No SQL, no clock, no
 * process spawn. The caller reads the row, calls outbox_step, and persists
 * the answer.
 */

enum {
    OUTBOX_BACKOFF_FIRST_S = 1,
    OUTBOX_BACKOFF_CAP_S = 3600,
    OUTBOX_HALT_CHAIN = 100,
    OUTBOX_DROP_DAYS = 7,
    OUTBOX_LANES = 4,
    OUTBOX_STEP_PAD = 6
};

typedef enum { OUTBOX_PENDING = 0, OUTBOX_DONE, OUTBOX_FAILED, OUTBOX_HALTED } OutboxState;

typedef enum {
    OUTBOX_RESULT_NONE = 0,        /* nothing reported: not run yet, or the process crashed */
    OUTBOX_RESULT_ONCE_EXIT,       /* a once-process ended; the exit code counts */
    OUTBOX_RESULT_PERSISTENT_ACK,  /* a persistent run sent an event.result with this event_id */
    OUTBOX_RESULT_PERSISTENT_NONE, /* a persistent run ended with no such event.result */
    OUTBOX_RESULT_PREFLIGHT        /* pre-flight failed: missing, not executable, untrusted */
} OutboxResult;

/* What next_retry_at must become on the row. */
typedef enum {
    OUTBOX_RETRY_KEEP = 0,     /* leave it as it is */
    OUTBOX_RETRY_CLEAR,        /* unset it */
    OUTBOX_RETRY_AFTER_BACKOFF /* now plus OutboxStep.backoff_s */
} OutboxRetryAt;

typedef struct {
    int64_t chain_len; /* cause.chain of the event; >= 0 */
    int64_t age_days;  /* whole days since the row was created; >= 0 */
    int64_t waited_s;  /* delay waited before the attempt reported; 0 on a first attempt; >= 0 */
    OutboxState state;
    OutboxResult result;
    int exit_code;   /* read for OUTBOX_RESULT_ONCE_EXIT only */
    bool event_done; /* this event_id is already done */
    char pad_[3];    /* explicit tail padding (kept -Wpadded-clean) */
} OutboxInput;

typedef struct {
    int64_t backoff_s; /* persist on the row; 0 on a fresh pending recipient */
    OutboxState state;
    OutboxRetryAt retry_at;
    bool run_now; /* the next dispatch may run this recipient now */
    bool delete_row;
    char pad_[OUTBOX_STEP_PAD]; /* explicit tail padding (kept -Wpadded-clean) */
} OutboxStep;

/* False, with *out untouched, when in or out is NULL, an enum is out of range,
   or a number that must be >= 0 is negative. */
bool outbox_step(const OutboxInput *in, OutboxStep *out);

size_t outbox_lane_count(void);
/* lane is a 0-based lane index: lanes 0..3 run, a 5th lane (index 4) does not. */
bool outbox_lane_allows(size_t lane);

#endif /* REMEMBER_OUTBOX_H */
