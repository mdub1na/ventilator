#include "WorkerSupervisor.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const uint64_t SECOND = UINT64_C(1000000000);

typedef enum { EXIT_OK, EXIT_FAILED, CRASH, STOPPED, CANCEL } Action;
typedef enum { BASELINE, CHANGED, READ_FAILED, READ_CRASH, SLOW_READ, READ_STOPPED } Reading;

typedef struct {
    char directory[128];
    Action operation;
    Action recovery;
    Reading reading;
    volatile sig_atomic_t cancel;
    volatile pid_t operation_pid;
    volatile unsigned operations;
    volatile unsigned recoveries;
    volatile unsigned reads;
    volatile unsigned intent_seen;
    volatile unsigned operation_gone;
} Fixture;

static SmcBaselineSnapshot baseline(void) {
    SmcBaselineSnapshot snapshot = {0};
    snapshot.mode[0] = 3;
    snapshot.mode[1] = 3;
    snapshot.temperatures_c[0] = 55;
    snapshot.temperatures_c[1] = 45;
    snapshot.temperatures_c[2] = 30;
    return snapshot;
}

static int run_action(Action action) {
    if (action == CRASH) (void)raise(SIGKILL);
    if (action == STOPPED) {
        (void)raise(SIGSTOP);
        for (;;) pause();
    }
    return action == EXIT_FAILED ? 1 : 0;
}

static int operate(void *context) {
    Fixture *fixture = context;
    fixture->operation_pid = getpid();
    ++fixture->operations;
    int dir = open(fixture->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    fixture->intent_seen = control_intent_read(dir) == CONTROL_INTENT_PENDING;
    if (dir >= 0) (void)close(dir);
    if (fixture->operation == CANCEL) {
        fixture->cancel = 1;
        for (;;) pause();
    }
    return run_action(fixture->operation);
}

static int recover(void *context) {
    Fixture *fixture = context;
    ++fixture->recoveries;
    // This check runs INSIDE the new process, before any simulated restoration.
    fixture->operation_gone = fixture->operation_pid > 0 &&
        kill(fixture->operation_pid, 0) != 0 && errno == ESRCH;
    return run_action(fixture->recovery);
}

static SmcBaselineResult read_snapshot(void *context, SmcBaselineSnapshot *snapshot) {
    Fixture *fixture = context;
    ++fixture->reads;
    if (fixture->reading == READ_CRASH) (void)raise(SIGKILL);
    if (fixture->reading == READ_STOPPED) (void)run_action(STOPPED);
    if (fixture->reading == SLOW_READ) {
        struct timespec delay = {.tv_sec = 5, .tv_nsec = 100000000};
        (void)nanosleep(&delay, NULL);
    }
    *snapshot = baseline();
    if (fixture->reading == CHANGED) snapshot->ftst = 1;
    return fixture->reading == READ_FAILED ? SMC_BASELINE_READ_FAILED : SMC_BASELINE_OK;
}

static Fixture *fixture_new(int *directory_fd) {
    Fixture *fixture = mmap(NULL, sizeof(*fixture), PROT_READ | PROT_WRITE,
                            MAP_ANON | MAP_SHARED, -1, 0);
    assert(fixture != MAP_FAILED);
    (void)snprintf(fixture->directory, sizeof(fixture->directory),
                    "/private/tmp/ventilator-supervisor-test.XXXXXX");
    assert(mkdtemp(fixture->directory) != NULL);
    *directory_fd = open(fixture->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    assert(*directory_fd >= 0);
    return fixture;
}

static void fixture_free(Fixture *fixture, int directory_fd) {
    // Test cleanup only; production clears intent exclusively with a recovery proof.
    if (control_intent_read(directory_fd) != CONTROL_INTENT_CLEAR) {
        assert(unlinkat(directory_fd, "control-intent-v1", 0) == 0);
    }
    assert(close(directory_fd) == 0);
    assert(rmdir(fixture->directory) == 0);
    assert(munmap(fixture, sizeof(*fixture)) == 0);
}

static ControlLease held_lease(void) {
    uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
    assert(now > 61 * SECOND);
    SmcBaselineSnapshot snapshot = baseline();
    ControlLease lease = {0};
    // Only initial admission is synthetic. The supervised proof uses REAL time.
    uint64_t started = now - CONTROL_LEASE_BASELINE_NS;
    control_lease_start(&lease, CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, &snapshot, started);
    for (unsigned index = 1; index < CONTROL_LEASE_REQUIRED_SAMPLES; ++index) {
        control_lease_sample(&lease, SMC_BASELINE_OK, &snapshot, started + index * SECOND);
    }
    assert(control_lease_claim(&lease, 7, now, true,
                               CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, &snapshot));
    return lease;
}

static WorkerSupervisorLimits limits(void) {
    return (WorkerSupervisorLimits){
        .operation_ns = UINT64_C(500000000),
        .recovery_ns = UINT64_C(500000000),
        .observation_ns = 75 * SECOND,
        .reap_ns = SECOND,
    };
}

static WorkerSupervisorReport run(int dir, ControlLease *lease, Fixture *fixture) {
    WorkerSupervisorBackend backend = {
        .context = fixture, .operate = operate, .recover = recover, .read = read_snapshot,
    };
    return worker_supervisor_run(dir, lease, backend, limits(), &fixture->cancel);
}

static void assert_reaped(pid_t pid) {
    int status = 0;
    assert(pid > 0 && waitpid(pid, &status, WNOHANG) == -1 && errno == ECHILD);
}

static void stopped_worker_is_reaped_before_recovery_and_fresh_minute(void) {
    int dir;
    Fixture *fixture = fixture_new(&dir);
    fixture->operation = STOPPED;
    ControlLease lease = held_lease();
    uint64_t started = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
    WorkerSupervisorReport report = run(dir, &lease, fixture);
    uint64_t elapsed = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - started;
    assert(report.result == SUPERVISOR_RECOVERED && report.operation == WORKER_TIMED_OUT);
    assert(report.reaped == 3 && fixture->operations == 1 && fixture->intent_seen);
    assert(fixture->recoveries == 1 && fixture->operation_gone);
    assert(fixture->reads == CONTROL_LEASE_REQUIRED_SAMPLES);
    assert(elapsed >= CONTROL_LEASE_BASELINE_NS && elapsed < 75 * SECOND);
    assert(report.operation_pid != report.recovery_pid && report.recovery_pid != report.observer_pid);
    assert_reaped(report.operation_pid);
    assert_reaped(report.recovery_pid);
    assert_reaped(report.observer_pid);
    assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR);
    assert(lease.state == CONTROL_LEASE_STABLE && !lease.recovery_verified);
    fixture_free(fixture, dir);
}

static void worker_exit_never_substitutes_for_independent_observation(void) {
    const Action actions[] = {EXIT_OK, EXIT_FAILED, CRASH, CANCEL};
    const WorkerExit expected[] = {WORKER_EXITED, WORKER_FAILED, WORKER_CRASHED, WORKER_CANCELLED};
    for (unsigned index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index) {
        int dir;
        Fixture *fixture = fixture_new(&dir);
        fixture->operation = actions[index];
        fixture->reading = CHANGED;
        ControlLease lease = held_lease();
        WorkerSupervisorReport report = run(dir, &lease, fixture);
        assert(report.result == SUPERVISOR_OBSERVATION_FAILED && report.operation == expected[index]);
        assert(report.reaped == 3 && fixture->intent_seen && fixture->operation_gone);
        assert(fixture->recoveries == 1 && fixture->reads >= 1);
        assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
        assert(!lease.recovery_verified);
        assert_reaped(report.operation_pid);
        assert_reaped(report.recovery_pid);
        assert_reaped(report.observer_pid);
        fixture_free(fixture, dir);
    }
}

static void failed_or_stopped_recovery_keeps_intent_and_skips_observation(void) {
    const Action actions[] = {EXIT_FAILED, CRASH, STOPPED};
    for (unsigned index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index) {
        int dir;
        Fixture *fixture = fixture_new(&dir);
        fixture->recovery = actions[index];
        ControlLease lease = held_lease();
        uint64_t started = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
        WorkerSupervisorReport report = run(dir, &lease, fixture);
        assert(report.result == SUPERVISOR_RECOVERY_FAILED && report.reaped == 2);
        assert(report.observer_pid == 0 && fixture->reads == 0 && fixture->operation_gone);
        assert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - started < 3 * SECOND);
        assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
        assert_reaped(report.operation_pid);
        assert_reaped(report.recovery_pid);
        fixture_free(fixture, dir);
    }
}

static void failed_crashed_or_slow_reader_never_clears_intent(void) {
    const Reading readings[] = {READ_FAILED, READ_CRASH, SLOW_READ, READ_STOPPED};
    for (unsigned index = 0; index < sizeof(readings) / sizeof(readings[0]); ++index) {
        int dir;
        Fixture *fixture = fixture_new(&dir);
        fixture->reading = readings[index];
        ControlLease lease = held_lease();
        uint64_t started = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
        WorkerSupervisorReport report = run(dir, &lease, fixture);
        assert(report.result == SUPERVISOR_OBSERVATION_FAILED && report.reaped == 3);
        assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
        assert(!lease.recovery_verified);
        assert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - started < 10 * SECOND);
        assert_reaped(report.observer_pid);
        fixture_free(fixture, dir);
    }
}

static void pending_stale_or_unreapable_admission_never_forks(void) {
    int dir;
    Fixture *fixture = fixture_new(&dir);
    ControlLease lease = held_lease();
    assert(control_intent_mark_pending(dir, &lease));
    WorkerSupervisorReport report = run(dir, &lease, fixture);
    assert(report.result == SUPERVISOR_BLOCKED && report.reaped == 0 && fixture->operations == 0);
    fixture_free(fixture, dir);

    fixture = fixture_new(&dir);
    lease = held_lease();
    lease.last_sample_ns -= 3 * SECOND;
    report = run(dir, &lease, fixture);
    assert(report.result == SUPERVISOR_BLOCKED && fixture->operations == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR);
    lease = held_lease();
    struct sigaction ignored = {.sa_handler = SIG_IGN};
    struct sigaction original = {0};
    assert(sigaction(SIGCHLD, &ignored, &original) == 0);
    report = run(dir, &lease, fixture);
    assert(sigaction(SIGCHLD, &original, NULL) == 0);
    assert(report.result == SUPERVISOR_BLOCKED && fixture->operations == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR);
    fixture_free(fixture, dir);
}

int main(int argc, char **argv) {
    (void)argv;
    // Fast-only mode also supports focused mutation checks without a minute wait.
    if (argc == 1) {
        puts("supervisor: stopped worker -> reaped -> recovery -> real 60-second observation");
        fflush(stdout);
        stopped_worker_is_reaped_before_recovery_and_fresh_minute();
    }
    worker_exit_never_substitutes_for_independent_observation();
    failed_or_stopped_recovery_keeps_intent_and_skips_observation();
    failed_crashed_or_slow_reader_never_clears_intent();
    pending_stale_or_unreapable_admission_never_forks();
    puts("worker supervisor process tests passed (no SMC access)");
    return 0;
}
