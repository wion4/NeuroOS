#include "journal.h"
#include <stdbool.h>
/* Single CPU: writers and readers execute with interrupts disabled. No IO,
   allocations or retained user pointers; a slow observer cannot block jobs. */
static struct nos_event records[NOS_EVENT_RETAINED];
static uint64_t last_sequence;
static bool sequence_exhausted;
void journal_reset(void) {
    last_sequence = 0;
    sequence_exhausted = false;
    for (unsigned i = 0; i < NOS_EVENT_RETAINED; ++i)
        records[i] = (struct nos_event){0};
}
void journal_append(const struct nos_event *event) {
    if (last_sequence == UINT64_MAX) {
        sequence_exhausted = true;
        return;
    }
    struct nos_event copy = *event;
    copy.sequence = ++last_sequence;
    records[(copy.sequence - 1) % NOS_EVENT_RETAINED] = copy;
}
int journal_read(struct nos_event_query *query) {
    if (query->version != NOS_EVENT_VERSION || !query->capacity ||
        query->capacity > NOS_EVENT_BATCH || query->after > last_sequence)
        return -22;
    struct nos_event_query out = {.version = NOS_EVENT_VERSION,
                                  .capacity = query->capacity,
                                  .after = query->after,
                                  .flags = sequence_exhausted ? NOS_EVENT_EXHAUSTED : 0};
    out.oldest = last_sequence > NOS_EVENT_RETAINED ? last_sequence - NOS_EVENT_RETAINED + 1 : 1;
    uint64_t cursor = query->after;
    if (cursor < out.oldest - 1) {
        out.lost = out.oldest - 1 - cursor;
        cursor = out.oldest - 1;
    }
    uint64_t available = last_sequence - cursor;
    out.count = available < out.capacity ? (uint32_t)available : out.capacity;
    for (unsigned i = 0; i < out.count; ++i) {
        uint64_t sequence = cursor + 1 + i;
        out.events[i] = records[(sequence - 1) % NOS_EVENT_RETAINED];
    }
    out.next = cursor + out.count; /* <= last_sequence, including UINT64_MAX. */
    *query = out;
    return 0;
}
