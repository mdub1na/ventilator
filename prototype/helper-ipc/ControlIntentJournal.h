#ifndef VENTILATOR_CONTROL_INTENT_JOURNAL_H
#define VENTILATOR_CONTROL_INTENT_JOURNAL_H

#include <stdbool.h>

typedef enum {
    CONTROL_INTENT_CLEAR,
    CONTROL_INTENT_PENDING,
    CONTROL_INTENT_UNKNOWN,
} ControlIntentStatus;

struct ControlLease;

// The caller supplies an already opened, private directory owned by its euid.
// These functions never accept a path or a client-provided identifier.
ControlIntentStatus control_intent_read(int directory_fd);
// Requires a held lease; call and verify this before any future SMC write.
bool control_intent_mark_pending(int directory_fd, struct ControlLease *lease);
// Consumes one completed recovery observation; startup observation is not proof.
bool control_intent_clear_verified(int directory_fd, struct ControlLease *lease);

#endif
