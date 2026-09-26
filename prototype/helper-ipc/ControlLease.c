#include "ControlLease.h"

#include <string.h>

static bool valid_read(SmcBaselineResult result, const SmcBaselineSnapshot *snapshot) {
    return result == SMC_BASELINE_OK && snapshot != NULL;
}

static bool cool_enough(const SmcBaselineSnapshot *snapshot) {
    for (unsigned index = 0; index < 3; ++index) {
        if (!smc_baseline_temperature_valid(snapshot->temperatures_c[index]) ||
            snapshot->temperatures_c[index] >= 75.0) return false;
    }
    return true;
}

static void require_recovery(ControlLease *lease) {
    lease->state = CONTROL_LEASE_RECOVERY_REQUIRED;
    lease->owner = 0;
    lease->expires_at_ns = 0;
    lease->recovery_verified = false;
}

void control_lease_start(ControlLease *lease, ControlIntentStatus intent,
                         SmcBaselineResult result,
                         const SmcBaselineSnapshot *snapshot, uint64_t now_ns) {
    if (lease == NULL) return;
    memset(lease, 0, sizeof(*lease));
    if (intent != CONTROL_INTENT_CLEAR) {
        lease->write_pending = true;
        require_recovery(lease);
    } else if (now_ns == 0 || !valid_read(result, snapshot)) {
        lease->state = CONTROL_LEASE_READ_FAILED;
    } else if (!smc_baseline_is_system(snapshot)) {
        require_recovery(lease);
    } else {
        lease->state = CONTROL_LEASE_OBSERVING;
        lease->observation_started_ns = now_ns;
        lease->last_sample_ns = now_ns;
        lease->samples = 1;
    }
}

void control_lease_sample(ControlLease *lease, SmcBaselineResult result,
                          const SmcBaselineSnapshot *snapshot, uint64_t now_ns) {
    if (lease == NULL ||
        (lease->state != CONTROL_LEASE_OBSERVING &&
         lease->state != CONTROL_LEASE_VERIFYING_RECOVERY)) return;
    if (!valid_read(result, snapshot) || now_ns <= lease->last_sample_ns ||
        now_ns - lease->last_sample_ns < CONTROL_LEASE_SAMPLE_MIN_NS) {
        lease->state = CONTROL_LEASE_READ_FAILED;
        return;
    }
    lease->last_sample_ns = now_ns;
    ++lease->samples;
    if (!smc_baseline_is_system(snapshot)) {
        require_recovery(lease);
        return;
    }
    if (lease->samples >= CONTROL_LEASE_REQUIRED_SAMPLES &&
        now_ns - lease->observation_started_ns >= CONTROL_LEASE_BASELINE_NS) {
        lease->recovery_verified = lease->state == CONTROL_LEASE_VERIFYING_RECOVERY;
        lease->state = CONTROL_LEASE_STABLE;
        lease->write_pending = false;
        lease->owner = 0;
        lease->expires_at_ns = 0;
    }
}

bool control_lease_claim(ControlLease *lease, uint64_t owner, uint64_t now_ns,
                         bool restore_protocol_verified, ControlIntentStatus intent,
                         SmcBaselineResult result,
                         const SmcBaselineSnapshot *snapshot) {
    if (lease == NULL || lease->state != CONTROL_LEASE_STABLE) return false;
    if (intent != CONTROL_INTENT_CLEAR) {
        require_recovery(lease);
        return false;
    }
    if (!restore_protocol_verified || owner == 0 || now_ns == 0 ||
        now_ns < lease->last_sample_ns ||
        now_ns - lease->last_sample_ns > UINT64_C(2000000000) ||
        now_ns > UINT64_MAX - CONTROL_LEASE_TTL_NS) return false;
    if (!valid_read(result, snapshot)) {
        lease->state = CONTROL_LEASE_READ_FAILED;
        return false;
    }
    if (!smc_baseline_is_system(snapshot) || !cool_enough(snapshot)) {
        require_recovery(lease);
        return false;
    }
    lease->state = CONTROL_LEASE_HELD;
    lease->owner = owner;
    lease->last_sample_ns = now_ns;
    lease->expires_at_ns = now_ns + CONTROL_LEASE_TTL_NS;
    lease->write_pending = false;
    return true;
}

bool control_lease_mark_write_pending(ControlLease *lease, uint64_t owner,
                                      uint64_t now_ns, ControlIntentStatus intent) {
    if (lease == NULL || lease->state != CONTROL_LEASE_HELD ||
        owner == 0 || owner != lease->owner) return false;
    if (intent != CONTROL_INTENT_PENDING ||
        now_ns < lease->last_sample_ns || now_ns >= lease->expires_at_ns) {
        require_recovery(lease);
        return false;
    }
    lease->write_pending = true;
    return true;
}

bool control_lease_prepare_persistent_intent(ControlLease *lease) {
    if (lease == NULL || lease->state != CONTROL_LEASE_HELD ||
        lease->owner == 0 || lease->write_pending) return false;
    lease->recovery_verified = false;
    return true;
}

bool control_lease_renew(ControlLease *lease, uint64_t owner, uint64_t now_ns,
                         ControlIntentStatus intent, SmcBaselineResult result,
                         const SmcBaselineSnapshot *snapshot) {
    if (lease == NULL || lease->state != CONTROL_LEASE_HELD ||
        owner == 0 || owner != lease->owner) return false;
    if (intent != (lease->write_pending ? CONTROL_INTENT_PENDING : CONTROL_INTENT_CLEAR) ||
        now_ns < lease->last_sample_ns || now_ns >= lease->expires_at_ns ||
        now_ns > UINT64_MAX - CONTROL_LEASE_TTL_NS ||
        !valid_read(result, snapshot) || !cool_enough(snapshot) ||
        (lease->write_pending && smc_baseline_is_system(snapshot)) ||
        (!lease->write_pending && !smc_baseline_is_system(snapshot))) {
        require_recovery(lease);
        return false;
    }
    lease->last_sample_ns = now_ns;
    lease->expires_at_ns = now_ns + CONTROL_LEASE_TTL_NS;
    return true;
}

void control_lease_owner_lost(ControlLease *lease, uint64_t owner) {
    if (lease != NULL && lease->state == CONTROL_LEASE_HELD &&
        owner != 0 && owner == lease->owner) require_recovery(lease);
}

void control_lease_tick(ControlLease *lease, uint64_t now_ns) {
    if (lease != NULL && lease->state == CONTROL_LEASE_HELD &&
        now_ns >= lease->expires_at_ns) require_recovery(lease);
}

void control_lease_sleep(ControlLease *lease) {
    if (lease == NULL) return;
    if (lease->state == CONTROL_LEASE_HELD ||
        lease->state == CONTROL_LEASE_STABLE ||
        lease->state == CONTROL_LEASE_OBSERVING ||
        lease->state == CONTROL_LEASE_VERIFYING_RECOVERY) {
        require_recovery(lease);
    }
}

void control_lease_recovery_started(ControlLease *lease, SmcBaselineResult result,
                                    const SmcBaselineSnapshot *snapshot,
                                    uint64_t now_ns) {
    if (lease == NULL || lease->state != CONTROL_LEASE_RECOVERY_REQUIRED) return;
    if (now_ns == 0 || !valid_read(result, snapshot)) {
        lease->state = CONTROL_LEASE_READ_FAILED;
    } else if (smc_baseline_is_system(snapshot)) {
        lease->recovery_verified = false;
        lease->state = CONTROL_LEASE_VERIFYING_RECOVERY;
        lease->observation_started_ns = now_ns;
        lease->last_sample_ns = now_ns;
        lease->samples = 1;
    }
}

bool control_lease_take_recovery_proof(ControlLease *lease) {
    if (lease == NULL || lease->state != CONTROL_LEASE_STABLE ||
        !lease->recovery_verified || lease->write_pending ||
        lease->owner != 0 || lease->expires_at_ns != 0 ||
        lease->samples < CONTROL_LEASE_REQUIRED_SAMPLES ||
        lease->last_sample_ns < lease->observation_started_ns ||
        lease->last_sample_ns - lease->observation_started_ns < CONTROL_LEASE_BASELINE_NS) {
        return false;
    }
    lease->recovery_verified = false;
    return true;
}

const char *control_lease_state_name(ControlLeaseState state) {
    switch (state) {
        case CONTROL_LEASE_OBSERVING: return "observing";
        case CONTROL_LEASE_STABLE: return "stable";
        case CONTROL_LEASE_HELD: return "held";
        case CONTROL_LEASE_RECOVERY_REQUIRED: return "recovery_required";
        case CONTROL_LEASE_VERIFYING_RECOVERY: return "verifying_recovery";
        case CONTROL_LEASE_READ_FAILED: return "read_failed";
    }
    return "unknown";
}
