#ifndef NEUROOS_EVENTS_H
#define NEUROOS_EVENTS_H
#include <stdint.h>
#define NOS_EVENT_VERSION 1u
#define NOS_EVENT_BATCH 8u
#define NOS_EVENT_RETAINED 64u
#define NOS_EVENT_EXHAUSTED 1u
#define NOS_EVENT_FILES 1u
#define NOS_EVENT_SCOPED_FILES 2u
/* Append new kinds; existing numeric values belong to format version 1. */
enum {
    NOS_EVENT_REQUESTED = 1,
    NOS_EVENT_STARTED,
    NOS_EVENT_LOAD_FAILED,
    NOS_EVENT_CANCEL_REQUESTED,
    NOS_EVENT_EXITED,
    NOS_EVENT_FAULTED,
    NOS_EVENT_TIMED_OUT,
    NOS_EVENT_CANCELLED,
    NOS_EVENT_FS_CLOSED,
    NOS_EVENT_FS_RECOVERING,
    NOS_EVENT_FS_RECOVERED,
    NOS_EVENT_FS_RECOVERY_FAILED,
    NOS_EVENT_REAPED,
    NOS_EVENT_OWNER_CLOSED
};
/* Numeric metadata only; no stdout, user pointers, filenames or file contents.
   cpu_ticks counts charged user timer ticks; pages is the job's peak owned-page
   count observed at load/heap changes. Neither is wall-clock CPU profiling. */
struct nos_event {
    uint64_t sequence, ticks, job, owner;
    uint32_t kind, flags;
    uint64_t value, cpu_ticks, pages;
};
/* Syscall 21: (query pointer, sizeof(query), 0), trusted controller only.
   Inputs: version, capacity (1..8), after. All other fields are outputs.
   Independent readers advance after=next. lost reports overwritten unread
   sequence numbers. Reading never removes records. Journal is volatile. */
struct nos_event_query {
    uint32_t version, capacity;
    uint64_t after, next, oldest, lost;
    uint32_t count, flags;
    struct nos_event events[NOS_EVENT_BATCH];
};
_Static_assert(sizeof(struct nos_event) == 64, "Event record ABI");
_Static_assert(sizeof(struct nos_event_query) == 560, "Event query ABI");
#endif
