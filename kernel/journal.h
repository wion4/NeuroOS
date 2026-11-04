#ifndef NEUROOS_JOURNAL_H
#define NEUROOS_JOURNAL_H
#include "../user/events.h"
void journal_reset(void);
void journal_append(const struct nos_event *event);
int journal_read(struct nos_event_query *query);
#endif
