#ifndef VENTILATOR_CONTROL_INTENT_JOURNAL_H
#define VENTILATOR_CONTROL_INTENT_JOURNAL_H

#include <stdbool.h>

typedef enum {
    CONTROL_INTENT_CLEAR,
    CONTROL_INTENT_PENDING,
    CONTROL_INTENT_UNKNOWN,
} ControlIntentStatus;

// The caller supplies an already opened, private directory owned by its euid.
// These functions never accept a path or a client-provided identifier.
ControlIntentStatus control_intent_read(int directory_fd);
// Call and verify this before any future SMC write. A failed mark forbids it.
bool control_intent_mark_pending(int directory_fd);
// Call only after a separate recovery protocol has verified the baseline.
bool control_intent_clear_verified(int directory_fd);

#endif
