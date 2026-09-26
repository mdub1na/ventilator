#ifndef VENTILATOR_CONTROL_LEASE_H
#define VENTILATOR_CONTROL_LEASE_H

#include "SmcBaselineRead.h"
#include "ControlIntentJournal.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    CONTROL_LEASE_REQUIRED_SAMPLES = 61,
};

#define CONTROL_LEASE_BASELINE_NS UINT64_C(60000000000)
#define CONTROL_LEASE_TTL_NS UINT64_C(15000000000)
#define CONTROL_LEASE_SAMPLE_MIN_NS UINT64_C(900000000)

typedef enum {
    CONTROL_LEASE_OBSERVING,
    CONTROL_LEASE_STABLE,
    CONTROL_LEASE_HELD,
    CONTROL_LEASE_RECOVERY_REQUIRED,
    CONTROL_LEASE_VERIFYING_RECOVERY,
    CONTROL_LEASE_READ_FAILED,
} ControlLeaseState;

typedef struct {
    ControlLeaseState state;
    uint64_t owner;
    uint64_t expires_at_ns;
    uint64_t observation_started_ns;
    uint64_t last_sample_ns;
    unsigned samples;
    bool write_pending;
} ControlLease;

// This reducer has no SMC writer and is not linked into the read-only daemon.
// The capability argument exists only for simulated future integration.
void control_lease_start(ControlLease *lease, ControlIntentStatus intent,
                         SmcBaselineResult result,
                         const SmcBaselineSnapshot *snapshot, uint64_t now_ns);
void control_lease_sample(ControlLease *lease, SmcBaselineResult result,
                          const SmcBaselineSnapshot *snapshot, uint64_t now_ns);
bool control_lease_claim(ControlLease *lease, uint64_t owner, uint64_t now_ns,
                         bool restore_protocol_verified, ControlIntentStatus intent,
                         SmcBaselineResult result,
                         const SmcBaselineSnapshot *snapshot);
bool control_lease_mark_write_pending(ControlLease *lease, uint64_t owner,
                                      uint64_t now_ns, ControlIntentStatus intent);
bool control_lease_renew(ControlLease *lease, uint64_t owner, uint64_t now_ns,
                         ControlIntentStatus intent, SmcBaselineResult result,
                         const SmcBaselineSnapshot *snapshot);
void control_lease_owner_lost(ControlLease *lease, uint64_t owner);
void control_lease_tick(ControlLease *lease, uint64_t now_ns);
void control_lease_sleep(ControlLease *lease);
void control_lease_recovery_started(ControlLease *lease, SmcBaselineResult result,
                                    const SmcBaselineSnapshot *snapshot,
                                    uint64_t now_ns);
const char *control_lease_state_name(ControlLeaseState state);

#endif
