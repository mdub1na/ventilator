#include <stdbool.h>
#include "WorkerSupervisor.h"
#include "SupervisorProbeStorage.h"
#include "SupervisorProbeDirectory.h"
#include "SupervisorProbeCrash.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const uint64_t SECOND = UINT64_C(1000000000);

typedef struct {
    int directory;
    bool crash_observer;
} ProbeContext;

static SmcBaselineResult read_baseline(void *context, SmcBaselineSnapshot *snapshot) {
    ProbeContext *probe = context;
    if (probe->crash_observer) {
        // The ordinary ownership monitor should exit this worker immediately
        // on parent EOF. A broken monitor still has a diagnostic failure bound.
        alarm(10);
        for (;;) pause();
    }
    return smc_baseline_read(snapshot);
}

static int check_only(void *context) {
    (void)context;
    SmcBaselineSnapshot snapshot = {0};
    return smc_baseline_read(&snapshot) == SMC_BASELINE_OK &&
           smc_baseline_is_system(&snapshot) ? 0 : 1;
}

static void observer_started(void *context, pid_t observer, bool recovering) {
    ProbeContext *probe = context;
    if (recovering && probe->crash_observer)
        (void)supervisor_probe_crash_after_observer_start(probe->directory, observer);
}

static uint64_t window_ns(const ControlLease *lease) {
    return lease->last_sample_ns >= lease->observation_started_ns ?
        lease->last_sample_ns - lease->observation_started_ns : 0;
}

int main(int argc, char **argv) {
    // This executable is not a fan writer. No environment flag can enable one.
    bool cleanup = argc == 2 && strcmp(argv[1], "--cleanup") == 0;
    bool crash = argc == 2 && strcmp(argv[1], "--crash-observer") == 0;
    bool resume_only = argc == 2 && strcmp(argv[1], "--resume-pending") == 0;
    if (getuid() != 0 || geteuid() != 0 || (argc != 1 && !cleanup && !crash && !resume_only)) {
        fputs("root read-only probe accepts only fixed diagnostic modes\n", stderr);
        return 2;
    }
    umask(0077);
    int directory = SupervisorProbeOpenDirectory();
    int lock = directory >= 0 ? supervisor_probe_lock(directory) : -1;
    if (lock < 0) {
        if (directory >= 0) (void)close(directory);
        fputs("private probe state is busy or unsafe\n", stderr);
        return 2;
    }
    if (cleanup) {
        bool clean = supervisor_probe_cleanup(directory);
        if (clean) clean = unlinkat(directory, "probe-launch-v1", 0) == 0 && rmdir(SupervisorProbeDirectoryPath) == 0;
        (void)close(lock);
        (void)close(directory);
        if (clean) puts("{\"cleaned\":true}");
        return clean ? 0 : 2;
    }

    ProbeContext context = {.directory = directory};
    WorkerSupervisorBackend backend = {.context = &context, .operate = check_only, .recover = check_only,
                                        .read = read_baseline, .observer_started = observer_started};
    WorkerSupervisorLimits limits = {.operation_ns = 5 * SECOND, .recovery_ns = 5 * SECOND,
                                     .observation_ns = 75 * SECOND, .reap_ns = 2 * SECOND};
    ControlLease lease = {0};
    WorkerSupervisorReport admission = {0}, report = {.result = SUPERVISOR_BLOCKED};
    uint64_t admission_duration = 0;
    unsigned admission_samples = 0;
    ControlIntentStatus intent = control_intent_read(directory);
    bool resumed = intent == CONTROL_INTENT_PENDING;
    if (resumed && !crash) {
        // With this read-only backend, recovery merely checks system mode.
        // A changed SMC baseline remains pending; no restore command is issued.
        report = worker_supervisor_resume(directory, &lease, backend, limits);
    } else if (intent == CONTROL_INTENT_CLEAR && !resume_only) {
        SmcBaselineSnapshot latest = {0};
        admission = worker_supervisor_observe_baseline(directory, &lease, &latest, backend, limits);
        admission_samples = lease.samples;
        admission_duration = window_ns(&lease);
        // The verified capability here is only check_only, never a hardware
        // restoration protocol. ControlLease stays private to this executable.
        if (admission.result == SUPERVISOR_BASELINE_READY &&
            control_lease_claim(&lease, (uint64_t)getpid(), clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW),
                true, control_intent_read(directory), SMC_BASELINE_OK, &latest)) {
            context.crash_observer = crash;
            report = worker_supervisor_run(directory, &lease, backend, limits, NULL);
        }
    }
    bool clear = control_intent_read(directory) == CONTROL_INTENT_CLEAR;
    const char *state = report.result == SUPERVISOR_RECOVERED && clear ? "verified" : clear ? "blocked" : "pending";
    unsigned recovery_samples = report.result == SUPERVISOR_RECOVERED ? lease.samples : 0;
    uint64_t recovery_duration = report.result == SUPERVISOR_RECOVERED ? window_ns(&lease) : 0;
    printf("{\"state\":\"%s\",\"runner_pid\":%ld,\"runner_uid\":%u,\"resumed\":%s,"
           "\"admission_pid\":%ld,\"admission_samples\":%u,\"admission_duration_ns\":%llu,"
           "\"operation_pid\":%ld,\"recovery_pid\":%ld,\"observer_pid\":%ld,\"reaped\":%u,"
           "\"recovery_samples\":%u,\"recovery_duration_ns\":%llu,\"journal_clear\":%s}\n",
           state, (long)getpid(), (unsigned)geteuid(), resumed ? "true" : "false",
           (long)admission.observer_pid, admission_samples, (unsigned long long)admission_duration,
           (long)report.operation_pid, (long)report.recovery_pid, (long)report.observer_pid,
           admission.reaped + report.reaped, recovery_samples, (unsigned long long)recovery_duration,
           clear ? "true" : "false");
    (void)close(lock);
    (void)close(directory);
    return 0; // Failure is reported explicitly; success never enables writes.
}
