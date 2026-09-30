#include "WorkerSupervisor.h"
#include "ControlIntentJournal.h"
#include "../smc-write-trial/trial_actions.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static const uint64_t SECOND = UINT64_C(1000000000);

typedef struct {
    char directory[128];
    uint8_t ftst;
    uint8_t mode[2];
    double target[2];
    double fake_seconds;
    bool reject_ftst_clear;
    bool relatch_during_observation;
    unsigned operations;
    unsigned recoveries;
    unsigned observer_reads;
    char writes[1024];
    size_t writes_length;
} Fixture;

static SmcBaselineSnapshot snapshot_from(Fixture *fixture) {
    SmcBaselineSnapshot snapshot = {0};
    snapshot.ftst = fixture->ftst;
    memcpy(snapshot.mode, fixture->mode, sizeof(snapshot.mode));
    memcpy(snapshot.target_rpm, fixture->target, sizeof(snapshot.target_rpm));
    snapshot.temperatures_c[0] = 55;
    snapshot.temperatures_c[1] = 45;
    snapshot.temperatures_c[2] = 30;
    return snapshot;
}

static void append_write(Fixture *fixture, const char *event) {
    size_t length = strlen(event);
    assert(fixture->writes_length + length < sizeof(fixture->writes));
    memcpy(fixture->writes + fixture->writes_length, event, length + 1);
    fixture->writes_length += length;
}

static bool write_mode(void *context, unsigned fan, uint8_t mode) {
    Fixture *fixture = context;
    char event[32];
    assert(fan < 2 && mode == 0);
    (void)snprintf(event, sizeof(event), "M%u=0;", fan);
    append_write(fixture, event);
    fixture->mode[fan] = mode;
    return true;
}

static bool write_target(void *context, unsigned fan, double rpm) {
    Fixture *fixture = context;
    char event[32];
    assert(fan < 2 && rpm == 0.0);
    (void)snprintf(event, sizeof(event), "T%u=0;", fan);
    append_write(fixture, event);
    fixture->target[fan] = 0;
    return true;
}

static bool write_ftst(void *context, uint8_t value) {
    Fixture *fixture = context;
    assert(value == 0);
    append_write(fixture, "Ftst=0;");
    if (fixture->reject_ftst_clear) return false;
    fixture->ftst = 0;
    if (fixture->target[0] == 0 && fixture->target[1] == 0) {
        fixture->mode[0] = 3;
        fixture->mode[1] = 3;
    }
    return true;
}

static bool trial_read(void *context, bool include_temperatures, TrialObservation *output) {
    Fixture *fixture = context;
    memset(output, 0, sizeof(*output));
    output->ftst = fixture->ftst;
    memcpy(output->mode, fixture->mode, sizeof(output->mode));
    memcpy(output->target_rpm, fixture->target, sizeof(output->target_rpm));
    if (include_temperatures) {
        output->temperatures_c[0] = 55;
        output->temperatures_c[1] = 45;
        output->temperatures_c[2] = 30;
        output->metrics_available = true;
    }
    return true;
}

static bool trial_wait(void *context, unsigned milliseconds) {
    ((Fixture *)context)->fake_seconds += milliseconds / 1000.0;
    return true;
}

static double trial_now(void *context) {
    return ((Fixture *)context)->fake_seconds;
}

static bool trial_stop(void *context) {
    (void)context;
    return false;
}

static int operate(void *context) {
    Fixture *fixture = context;
    ++fixture->operations;
    // Model the changed state seen by recovery after an accepted Ftst write.
    // No IOKit writer is linked here.
    fixture->ftst = 1;
    fixture->mode[0] = 0;
    fixture->mode[1] = 0;
    fixture->target[0] = 1350;
    fixture->target[1] = 1458;
    return 1; // An operation error still requires a fresh recovery process.
}

static int recover(void *context) {
    Fixture *fixture = context;
    ++fixture->recoveries;
    TrialBackend backend = {
        .context = fixture,
        .write_mode = write_mode,
        .write_target = write_target,
        .write_ftst = write_ftst,
        .read_observation = trial_read,
        .wait_milliseconds = trial_wait,
        .monotonic_seconds = trial_now,
        .should_stop = trial_stop,
    };
    return trial_release_ftst_for_external_observation(&backend) ? 0 : 1;
}

static SmcBaselineResult observe(void *context, SmcBaselineSnapshot *snapshot) {
    Fixture *fixture = context;
    ++fixture->observer_reads;
    if (fixture->relatch_during_observation && fixture->observer_reads == 5) {
        fixture->ftst = 1;
        fixture->mode[0] = 0;
        fixture->mode[1] = 0;
        fixture->target[0] = 1350;
        fixture->target[1] = 1458;
    }
    *snapshot = snapshot_from(fixture);
    return SMC_BASELINE_OK;
}

static Fixture *fixture_new(int *directory_fd) {
    Fixture *fixture = mmap(NULL, sizeof(*fixture), PROT_READ | PROT_WRITE,
                            MAP_ANON | MAP_SHARED, -1, 0);
    assert(fixture != MAP_FAILED);
    (void)snprintf(fixture->directory, sizeof(fixture->directory),
                   "/private/tmp/ventilator-ftst-contract.XXXXXX");
    assert(mkdtemp(fixture->directory) != NULL);
    *directory_fd = open(fixture->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    assert(*directory_fd >= 0);
    fixture->mode[0] = 3;
    fixture->mode[1] = 3;
    return fixture;
}

static void fixture_free(Fixture *fixture, int directory_fd) {
    if (control_intent_read(directory_fd) != CONTROL_INTENT_CLEAR) {
        assert(unlinkat(directory_fd, "control-intent-v1", 0) == 0);
    }
    const char *files[] = {"supervisor-owner-v1", "supervisor-worker-v1", "supervisor-worker.next"};
    for (unsigned index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
        assert(unlinkat(directory_fd, files[index], 0) == 0 || errno == ENOENT);
    }
    assert(close(directory_fd) == 0);
    assert(rmdir(fixture->directory) == 0);
    assert(munmap(fixture, sizeof(*fixture)) == 0);
}

static ControlLease held_lease(Fixture *fixture) {
    SmcBaselineSnapshot snapshot = snapshot_from(fixture);
    ControlLease lease = {0};
    control_lease_start(&lease, CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, &snapshot, SECOND);
    for (unsigned index = 1; index < CONTROL_LEASE_REQUIRED_SAMPLES; ++index) {
        control_lease_sample(&lease, SMC_BASELINE_OK, &snapshot, (index + 1) * SECOND);
    }
    assert(control_lease_claim(&lease, 7, 62 * SECOND, true,
                               CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, &snapshot));
    uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
    lease.last_sample_ns = now;
    lease.expires_at_ns = now + CONTROL_LEASE_TTL_NS;
    return lease;
}

static WorkerSupervisorReport run(Fixture *fixture, int directory_fd, ControlLease *lease) {
    WorkerSupervisorBackend backend = {
        .context = fixture, .operate = operate, .recover = recover, .read = observe,
    };
    WorkerSupervisorLimits limits = {
        .operation_ns = SECOND,
        .recovery_ns = 5 * SECOND,
        .observation_ns = 75 * SECOND,
        .reap_ns = SECOND,
    };
    return worker_supervisor_run(directory_fd, lease, backend, limits, NULL);
}

static void accepted_effect_requires_external_minute(void) {
    int directory_fd;
    Fixture *fixture = fixture_new(&directory_fd);
    ControlLease lease = held_lease(fixture);
    uint64_t started = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
    WorkerSupervisorReport report = run(fixture, directory_fd, &lease);
    uint64_t elapsed = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - started;
    assert(report.result == SUPERVISOR_RECOVERED);
    assert(report.operation == WORKER_FAILED);
    assert(fixture->operations == 1 && fixture->recoveries == 1);
    assert(strcmp(fixture->writes, "T0=0;T1=0;Ftst=0;") == 0);
    assert(fixture->observer_reads == CONTROL_LEASE_REQUIRED_SAMPLES);
    assert(elapsed >= CONTROL_LEASE_BASELINE_NS);
    assert(control_intent_read(directory_fd) == CONTROL_INTENT_CLEAR);
    fixture_free(fixture, directory_fd);
}

static void rejected_release_preserves_pending_without_observation(void) {
    int directory_fd;
    Fixture *fixture = fixture_new(&directory_fd);
    fixture->reject_ftst_clear = true;
    ControlLease lease = held_lease(fixture);
    WorkerSupervisorReport report = run(fixture, directory_fd, &lease);
    assert(report.result == SUPERVISOR_RECOVERY_FAILED);
    assert(fixture->operations == 1 && fixture->recoveries == 1);
    assert(fixture->observer_reads == 0 && fixture->ftst == 1);
    assert(control_intent_read(directory_fd) == CONTROL_INTENT_PENDING);
    fixture_free(fixture, directory_fd);
}

static void delayed_effect_during_external_observation_preserves_pending(void) {
    int directory_fd;
    Fixture *fixture = fixture_new(&directory_fd);
    fixture->relatch_during_observation = true;
    ControlLease lease = held_lease(fixture);
    WorkerSupervisorReport report = run(fixture, directory_fd, &lease);
    assert(report.result == SUPERVISOR_OBSERVATION_FAILED);
    assert(fixture->operations == 1 && fixture->recoveries == 1);
    assert(fixture->observer_reads == 5 && fixture->ftst == 1);
    assert(control_intent_read(directory_fd) == CONTROL_INTENT_PENDING);
    fixture_free(fixture, directory_fd);
}

int main(void) {
    rejected_release_preserves_pending_without_observation();
    delayed_effect_during_external_observation_preserves_pending();
    accepted_effect_requires_external_minute();
    puts("supervised Ftst contract tests passed");
    return 0;
}
