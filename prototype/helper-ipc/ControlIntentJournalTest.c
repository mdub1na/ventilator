#include "ControlIntentJournal.h"
#include "ControlLease.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char marker_name[] = "control-intent-v1";
static const uint64_t SECOND = UINT64_C(1000000000);

static SmcBaselineSnapshot baseline_snapshot(void) {
    SmcBaselineSnapshot snapshot = {0};
    snapshot.mode[0] = 3;
    snapshot.mode[1] = 3;
    snapshot.temperatures_c[0] = 55;
    snapshot.temperatures_c[1] = 45;
    snapshot.temperatures_c[2] = 30;
    return snapshot;
}

static void finish_window(ControlLease *lease, const SmcBaselineSnapshot *snapshot,
                          uint64_t started) {
    for (unsigned second = 1; second < CONTROL_LEASE_REQUIRED_SAMPLES; ++second) {
        control_lease_sample(lease, SMC_BASELINE_OK, snapshot,
                             started + second * SECOND);
    }
}

static ControlLease held_lease(const SmcBaselineSnapshot *baseline) {
    ControlLease lease = {0};
    control_lease_start(&lease, CONTROL_INTENT_CLEAR, SMC_BASELINE_OK,
                        baseline, SECOND);
    finish_window(&lease, baseline, SECOND);
    assert(lease.state == CONTROL_LEASE_STABLE && !lease.recovery_verified);
    assert(control_lease_claim(&lease, 7, 61 * SECOND, true,
                               CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, baseline));
    return lease;
}

static ControlLease verified_recovery(const SmcBaselineSnapshot *baseline) {
    ControlLease lease = {0};
    control_lease_start(&lease, CONTROL_INTENT_PENDING, SMC_BASELINE_OK,
                        baseline, SECOND);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    control_lease_recovery_started(&lease, SMC_BASELINE_OK, baseline, 2 * SECOND);
    finish_window(&lease, baseline, 2 * SECOND);
    assert(lease.state == CONTROL_LEASE_STABLE && lease.recovery_verified);
    return lease;
}

int main(void) {
    char directory[] = "/private/tmp/ventilator-intent-test.XXXXXX";
    assert(mkdtemp(directory) != NULL);
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    assert(dir >= 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR);
    assert(control_intent_read(-1) == CONTROL_INTENT_UNKNOWN);
    SmcBaselineSnapshot baseline = baseline_snapshot();
    ControlLease first = held_lease(&baseline);
    assert(!control_intent_clear_verified(dir, &first));
    assert(!control_intent_mark_pending(dir, NULL));
    assert(control_intent_mark_pending(dir, &first));
    assert(control_lease_mark_write_pending(&first, 7, 61 * SECOND,
                                            CONTROL_INTENT_PENDING));
    assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
    ControlLease other = held_lease(&baseline);
    assert(!control_intent_mark_pending(dir, &other));
    assert(close(dir) == 0);

    // A different process reads the marker without inheriting lease memory.
    dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    assert(dir >= 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        int reopened = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        _exit(reopened >= 0 && control_intent_read(reopened) == CONTROL_INTENT_PENDING ? 0 : 1);
    }
    int child_status = 0;
    assert(waitpid(child, &child_status, 0) == child);
    assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    ControlIntentStatus intent = control_intent_read(dir);
    assert(intent == CONTROL_INTENT_PENDING);
    ControlLease lease = {0};
    control_lease_start(&lease, intent, SMC_BASELINE_OK, &baseline, SECOND);
    assert(lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    assert(!control_intent_clear_verified(dir, &lease));
    control_lease_recovery_started(&lease, SMC_BASELINE_OK, &baseline, 2 * SECOND);
    control_lease_sample(&lease, SMC_BASELINE_OK, &baseline, 3 * SECOND);
    assert(!control_intent_clear_verified(dir, &lease));
    // The full helper starts at second 2; repeat only the remaining samples.
    for (unsigned second = 2; second < CONTROL_LEASE_REQUIRED_SAMPLES; ++second) {
        control_lease_sample(&lease, SMC_BASELINE_OK, &baseline,
                             2 * SECOND + second * SECOND);
    }
    assert(lease.state == CONTROL_LEASE_STABLE && lease.recovery_verified);
    ControlLease denied = lease;
    assert(!control_lease_claim(&denied, 7, 62 * SECOND, true,
                                control_intent_read(dir), SMC_BASELINE_OK, &baseline));
    assert(denied.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    assert(!control_intent_clear_verified(dir, &denied));
    assert(control_intent_clear_verified(dir, &lease));
    assert(!lease.recovery_verified);
    assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR);
    assert(control_lease_claim(&lease, 7, 62 * SECOND, true,
                               control_intent_read(dir), SMC_BASELINE_OK, &baseline));

    assert(control_intent_mark_pending(dir, &lease));
    assert(!control_intent_clear_verified(dir, &lease));
    assert(fchmodat(dir, marker_name, 0644, 0) == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_UNKNOWN);
    ControlLease proof = verified_recovery(&baseline);
    assert(!control_intent_clear_verified(dir, &proof));
    assert(fchmodat(dir, marker_name, 0600, 0) == 0);
    assert(control_intent_clear_verified(dir, &proof));
    assert(!control_intent_clear_verified(dir, &proof));

    int malformed = openat(dir, marker_name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(malformed >= 0);
    assert(write(malformed, "bad", 3) == 3);
    assert(close(malformed) == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_UNKNOWN);
    ControlLease bad = held_lease(&baseline);
    assert(!control_intent_mark_pending(dir, &bad));
    assert(!control_intent_clear_verified(dir, &bad));
    assert(unlinkat(dir, marker_name, 0) == 0);

    assert(symlinkat("/etc/passwd", dir, marker_name) == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_UNKNOWN);
    bad = held_lease(&baseline);
    assert(!control_intent_mark_pending(dir, &bad));
    assert(unlinkat(dir, marker_name, 0) == 0);

    assert(fchmod(dir, 0777) == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_UNKNOWN);
    bad = held_lease(&baseline);
    assert(!control_intent_mark_pending(dir, &bad));
    assert(fchmod(dir, 0700) == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR);
    assert(close(dir) == 0);
    assert(rmdir(directory) == 0);
    puts("control intent journal tests passed");
    return 0;
}
