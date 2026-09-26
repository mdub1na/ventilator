#include "ControlLease.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint64_t SECOND = UINT64_C(1000000000);

static SmcBaselineSnapshot system_snapshot(void) {
    SmcBaselineSnapshot snapshot = {0};
    snapshot.mode[0] = 3;
    snapshot.mode[1] = 3;
    snapshot.temperatures_c[0] = 55;
    snapshot.temperatures_c[1] = 45;
    snapshot.temperatures_c[2] = 30;
    return snapshot;
}

static SmcBaselineSnapshot altered_snapshot(void) {
    SmcBaselineSnapshot snapshot = system_snapshot();
    snapshot.ftst = 1;
    snapshot.mode[0] = 0;
    snapshot.mode[1] = 0;
    snapshot.target_rpm[0] = 1350;
    snapshot.target_rpm[1] = 1458;
    return snapshot;
}

static uint64_t finish_baseline_window(ControlLease *lease, uint64_t start,
                                       const SmcBaselineSnapshot *snapshot) {
    for (unsigned second = 1; second < CONTROL_LEASE_REQUIRED_SAMPLES; ++second) {
        control_lease_sample(lease, SMC_BASELINE_OK, snapshot, start + second * SECOND);
    }
    return start + CONTROL_LEASE_BASELINE_NS;
}

static uint64_t stable_lease(ControlLease *lease, SmcBaselineSnapshot *snapshot) {
    *snapshot = system_snapshot();
    uint64_t start = SECOND;
    control_lease_start(lease, SMC_BASELINE_OK, snapshot, start);
    return finish_baseline_window(lease, start, snapshot);
}

static void startup_requires_the_full_window_and_explicit_capability(void) {
    ControlLease lease = {0};
    SmcBaselineSnapshot snapshot = system_snapshot();
    control_lease_start(&lease, SMC_BASELINE_OK, &snapshot, SECOND);
    assert(lease.state == CONTROL_LEASE_OBSERVING && lease.samples == 1);
    assert(!control_lease_claim(&lease, 7, 2 * SECOND, true,
                                SMC_BASELINE_OK, &snapshot));
    for (unsigned second = 1; second < 60; ++second) {
        control_lease_sample(&lease, SMC_BASELINE_OK, &snapshot,
                             SECOND + second * SECOND);
    }
    assert(lease.state == CONTROL_LEASE_OBSERVING && lease.samples == 60);
    control_lease_sample(&lease, SMC_BASELINE_OK, &snapshot, 61 * SECOND);
    assert(lease.state == CONTROL_LEASE_STABLE && lease.samples == 61);
    assert(!control_lease_claim(&lease, 7, 61 * SECOND, false,
                                SMC_BASELINE_OK, &snapshot));
    assert(control_lease_claim(&lease, 7, 61 * SECOND, true,
                               SMC_BASELINE_OK, &snapshot));
    assert(lease.state == CONTROL_LEASE_HELD && lease.owner == 7);
    assert(!control_lease_claim(&lease, 8, 61 * SECOND, true,
                                SMC_BASELINE_OK, &snapshot));
    assert(lease.owner == 7);
}

static void changed_startup_never_grants_from_one_later_baseline(void) {
    ControlLease lease = {0};
    SmcBaselineSnapshot changed = altered_snapshot();
    SmcBaselineSnapshot baseline = system_snapshot();
    control_lease_start(&lease, SMC_BASELINE_OK, &changed, SECOND);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    control_lease_sample(&lease, SMC_BASELINE_OK, &baseline, 2 * SECOND);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    assert(!control_lease_claim(&lease, 7, 2 * SECOND, true,
                                SMC_BASELINE_OK, &baseline));
    control_lease_recovery_started(&lease, SMC_BASELINE_OK, &baseline, 3 * SECOND);
    assert(lease.state == CONTROL_LEASE_VERIFYING_RECOVERY && lease.samples == 1);
    finish_baseline_window(&lease, 3 * SECOND, &baseline);
    assert(lease.state == CONTROL_LEASE_STABLE);
}

static void write_intent_and_client_loss_require_new_verification(void) {
    ControlLease lease = {0};
    SmcBaselineSnapshot baseline = {0};
    uint64_t now = stable_lease(&lease, &baseline);
    assert(control_lease_claim(&lease, 23, now, true, SMC_BASELINE_OK, &baseline));
    assert(!control_lease_mark_write_pending(&lease, 99, now));
    assert(control_lease_mark_write_pending(&lease, 23, now));
    assert(lease.write_pending);
    control_lease_owner_lost(&lease, 99);
    assert(lease.state == CONTROL_LEASE_HELD);
    control_lease_owner_lost(&lease, 23);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED && lease.write_pending);
    assert(!control_lease_renew(&lease, 23, now + SECOND,
                                SMC_BASELINE_OK, &baseline));
    control_lease_recovery_started(&lease, SMC_BASELINE_OK, &baseline,
                                   now + SECOND);
    assert(lease.state == CONTROL_LEASE_VERIFYING_RECOVERY);
    SmcBaselineSnapshot changed = altered_snapshot();
    control_lease_sample(&lease, SMC_BASELINE_OK, &changed, now + 2 * SECOND);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    control_lease_recovery_started(&lease, SMC_BASELINE_OK, &baseline,
                                   now + 3 * SECOND);
    finish_baseline_window(&lease, now + 3 * SECOND, &baseline);
    assert(lease.state == CONTROL_LEASE_STABLE && !lease.write_pending);
}

static void deadline_and_sleep_fail_closed(void) {
    ControlLease lease = {0};
    SmcBaselineSnapshot baseline = {0};
    uint64_t now = stable_lease(&lease, &baseline);
    assert(control_lease_claim(&lease, 31, now, true, SMC_BASELINE_OK, &baseline));
    control_lease_tick(&lease, now + CONTROL_LEASE_TTL_NS - 1);
    assert(lease.state == CONTROL_LEASE_HELD);
    control_lease_tick(&lease, now + CONTROL_LEASE_TTL_NS);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);

    now = stable_lease(&lease, &baseline);
    assert(control_lease_claim(&lease, 31, now, true, SMC_BASELINE_OK, &baseline));
    assert(control_lease_mark_write_pending(&lease, 31, now));
    control_lease_sleep(&lease);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    control_lease_start(&lease, SMC_BASELINE_OK, &baseline, now + SECOND);
    assert(lease.state == CONTROL_LEASE_OBSERVING && !lease.write_pending);
}

static void system_reclaim_and_stale_reads_end_or_deny_a_lease(void) {
    ControlLease lease = {0};
    SmcBaselineSnapshot baseline = {0};
    uint64_t now = stable_lease(&lease, &baseline);
    assert(!control_lease_claim(&lease, 37, now + 3 * SECOND, true,
                                SMC_BASELINE_OK, &baseline));
    assert(lease.state == CONTROL_LEASE_STABLE);
    assert(control_lease_claim(&lease, 37, now, true,
                               SMC_BASELINE_OK, &baseline));
    assert(!control_lease_renew(&lease, 99, now + SECOND,
                                SMC_BASELINE_OK, &baseline));
    assert(lease.state == CONTROL_LEASE_HELD);
    assert(control_lease_mark_write_pending(&lease, 37, now));
    assert(!control_lease_renew(&lease, 37, now + SECOND,
                                SMC_BASELINE_OK, &baseline));
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);

    now = stable_lease(&lease, &baseline);
    assert(control_lease_claim(&lease, 37, now, true,
                               SMC_BASELINE_OK, &baseline));
    assert(!control_lease_renew(&lease, 37, now - 1,
                                SMC_BASELINE_OK, &baseline));
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);

    now = stable_lease(&lease, &baseline);
    assert(control_lease_claim(&lease, 37, now, true,
                               SMC_BASELINE_OK, &baseline));
    SmcBaselineSnapshot changed = altered_snapshot();
    assert(!control_lease_renew(&lease, 37, now + SECOND,
                                SMC_BASELINE_OK, &changed));
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
}

static void invalid_reads_timing_and_temperature_never_grant(void) {
    ControlLease lease = {0};
    SmcBaselineSnapshot baseline = system_snapshot();
    control_lease_start(&lease, SMC_BASELINE_READ_FAILED, NULL, SECOND);
    assert(lease.state == CONTROL_LEASE_READ_FAILED);
    control_lease_start(&lease, SMC_BASELINE_OK, &baseline, SECOND);
    control_lease_sample(&lease, SMC_BASELINE_OK, &baseline, SECOND);
    assert(lease.state == CONTROL_LEASE_READ_FAILED);

    uint64_t now = stable_lease(&lease, &baseline);
    baseline.temperatures_c[0] = 75;
    assert(!control_lease_claim(&lease, 41, now, true, SMC_BASELINE_OK, &baseline));
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);

    now = stable_lease(&lease, &baseline);
    assert(!control_lease_claim(&lease, 41, now, false, SMC_BASELINE_OK, &baseline));
    assert(lease.state == CONTROL_LEASE_STABLE);
    assert(control_lease_claim(&lease, 41, now, true, SMC_BASELINE_OK, &baseline));
    baseline.temperatures_c[1] = 75;
    assert(!control_lease_renew(&lease, 41, now + SECOND,
                                SMC_BASELINE_OK, &baseline));
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
}

static void restart_rechecks_hardware_instead_of_reusing_memory(void) {
    ControlLease lease = {0};
    SmcBaselineSnapshot baseline = {0};
    uint64_t now = stable_lease(&lease, &baseline);
    assert(control_lease_claim(&lease, 51, now, true, SMC_BASELINE_OK, &baseline));
    assert(control_lease_mark_write_pending(&lease, 51, now));
    SmcBaselineSnapshot changed = altered_snapshot();
    control_lease_start(&lease, SMC_BASELINE_OK, &changed, now + SECOND);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    assert(lease.owner == 0 && lease.samples == 0);
    control_lease_start(&lease, SMC_BASELINE_OK, &baseline, now + 2 * SECOND);
    assert(lease.state == CONTROL_LEASE_OBSERVING);
    assert(!control_lease_claim(&lease, 51, now + 2 * SECOND, true,
                                SMC_BASELINE_OK, &baseline));
}

int main(void) {
    startup_requires_the_full_window_and_explicit_capability();
    changed_startup_never_grants_from_one_later_baseline();
    write_intent_and_client_loss_require_new_verification();
    deadline_and_sleep_fail_closed();
    system_reclaim_and_stale_reads_end_or_deny_a_lease();
    invalid_reads_timing_and_temperature_never_grant();
    restart_rechecks_hardware_instead_of_reusing_memory();
    assert(strcmp(control_lease_state_name(CONTROL_LEASE_RECOVERY_REQUIRED),
                  "recovery_required") == 0);
    puts("control lease tests passed");
    return 0;
}
