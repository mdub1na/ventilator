#include "WorkerSupervisor.h"
#include "SupervisorOwnership.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const uint64_t SECOND = UINT64_C(1000000000);

typedef enum { EXIT_OK, EXIT_FAILED, CRASH, STOPPED, CANCEL, BUSY } Action;
typedef enum { BASELINE, CHANGED, READ_FAILED, READ_CRASH, SLOW_READ, READ_STOPPED, READ_BUSY } Reading;

typedef struct {
    char directory[128];
    Action operation;
    Action recovery;
    Reading reading;
    volatile sig_atomic_t cancel;
    volatile pid_t operation_pid;
    volatile pid_t recovery_pid;
    volatile pid_t reader_pid;
    volatile unsigned operations;
    volatile unsigned recoveries;
    volatile unsigned reads;
    volatile unsigned intent_seen;
    volatile unsigned record_seen;
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
    if (action == BUSY) for (;;) pause();
    return action == EXIT_FAILED ? 1 : 0;
}

static int operate(void *context) {
    Fixture *fixture = context;
    fixture->operation_pid = getpid();
    ++fixture->operations;
    int dir = open(fixture->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    fixture->intent_seen = control_intent_read(dir) == CONTROL_INTENT_PENDING;
    int record = openat(dir, "supervisor-worker-v1", O_RDONLY | O_NOFOLLOW);
    char contents[64] = {0};
    if (record >= 0) {
        long recorded = -1;
        ssize_t count = read(record, contents, sizeof(contents) - 1);
        fixture->record_seen = count > 0 &&
            sscanf(contents, "Ventilator worker v1 %ld", &recorded) == 1 && recorded == getpid();
        (void)close(record);
    }
    if (dir >= 0) (void)close(dir);
    if (fixture->operation == CANCEL) {
        fixture->cancel = 1;
        for (;;) pause();
    }
    return run_action(fixture->operation);
}

static int recover(void *context) {
    Fixture *fixture = context;
    fixture->recovery_pid = getpid();
    ++fixture->recoveries;
    // This check runs INSIDE the new process, before any simulated restoration.
    fixture->operation_gone = fixture->operation_pid > 0 &&
        kill(fixture->operation_pid, 0) != 0 && errno == ESRCH;
    return run_action(fixture->recovery);
}

static SmcBaselineResult read_snapshot(void *context, SmcBaselineSnapshot *snapshot) {
    Fixture *fixture = context;
    fixture->reader_pid = getpid();
    ++fixture->reads;
    if (fixture->reading == READ_CRASH) (void)raise(SIGKILL);
    if (fixture->reading == READ_STOPPED) (void)run_action(STOPPED);
    if (fixture->reading == READ_BUSY) (void)run_action(BUSY);
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
    const char *files[] = {"supervisor-owner-v1", "supervisor-worker-v1", "supervisor-worker.next"};
    for (unsigned index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
        assert(unlinkat(directory_fd, files[index], 0) == 0 || errno == ENOENT);
    }
    assert(close(directory_fd) == 0);
    assert(rmdir(fixture->directory) == 0);
    assert(munmap(fixture, sizeof(*fixture)) == 0);
}

static ControlLease held_lease(void) {
    uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
    assert(now > 0);
    SmcBaselineSnapshot snapshot = baseline();
    ControlLease lease = {0};
    // Only initial admission is synthetic. The supervised proof uses REAL time.
    uint64_t started = SECOND;
    control_lease_start(&lease, CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, &snapshot, started);
    for (unsigned index = 1; index < CONTROL_LEASE_REQUIRED_SAMPLES; ++index) {
        control_lease_sample(&lease, SMC_BASELINE_OK, &snapshot, started + index * SECOND);
    }
    assert(control_lease_claim(&lease, 7, 61 * SECOND, true,
                               CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, &snapshot));
    // Admission is simulated, so its HELD deadline is anchored to the real
    // clock separately. A freshly booted CI host need not already be 61s old.
    lease.last_sample_ns = now;
    lease.expires_at_ns = now + CONTROL_LEASE_TTL_NS;
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
    assert(report.reaped == 3 && fixture->operations == 1 && fixture->intent_seen && fixture->record_seen);
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
        assert(report.reaped == 3 && fixture->intent_seen && fixture->record_seen && fixture->operation_gone);
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

static void wait_for_pid(volatile pid_t *slot) {
    uint64_t deadline = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) + 5 * SECOND;
    while (*slot <= 0) {
        assert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) < deadline);
        struct timespec delay = {.tv_nsec = 10000000};
        (void)nanosleep(&delay, NULL);
    }
}

static void wait_for_absence(pid_t child) {
    uint64_t deadline = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) + 5 * SECOND;
    for (;;) {
        struct proc_bsdinfo info = {0};
        errno = 0;
        int size = proc_pidinfo(child, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
        if (size == 0 && errno == ESRCH) return;
        assert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) < deadline);
        struct timespec delay = {.tv_nsec = 10000000};
        (void)nanosleep(&delay, NULL);
    }
}

static pid_t start_owner(int dir, Fixture *fixture) {
    pid_t owner = fork();
    assert(owner >= 0);
    if (owner == 0) {
        ControlLease lease = held_lease();
        WorkerSupervisorLimits bound = limits();
        bound.operation_ns = 10 * SECOND;
        bound.recovery_ns = 10 * SECOND;
        WorkerSupervisorBackend backend = {
            .context = fixture, .operate = operate, .recover = recover, .read = read_snapshot,
        };
        (void)worker_supervisor_run(dir, &lease, backend, bound, NULL);
        _exit(1); // The test must kill the owner while its phase is still active.
    }
    return owner;
}

static void kill_owner(pid_t owner) {
    assert(kill(owner, SIGKILL) == 0);
    int status = 0;
    assert(waitpid(owner, &status, 0) == owner && WIFSIGNALED(status));
}

static WorkerSupervisorReport resume(int dir, ControlLease *lease, Fixture *fixture) {
    WorkerSupervisorBackend backend = {.context = fixture, .recover = recover, .read = read_snapshot};
    return worker_supervisor_resume(dir, lease, backend, limits());
}

static WorkerSupervisorReport resume_when_available(int dir, ControlLease *lease, Fixture *fixture) {
    uint64_t deadline = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) + 5 * SECOND;
    for (;;) {
        WorkerSupervisorReport report = resume(dir, lease, fixture);
        if (report.result != SUPERVISOR_BLOCKED) return report;
        assert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) < deadline);
        struct timespec delay = {.tv_nsec = 10000000};
        (void)nanosleep(&delay, NULL);
    }
}

static void owner_crash_stops_each_phase_and_restart_uses_a_fresh_window(bool full) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        int dir;
        Fixture *fixture = fixture_new(&dir);
        fixture->operation = phase == 0 ? BUSY : EXIT_OK;
        fixture->recovery = phase == 1 ? BUSY : EXIT_OK;
        fixture->reading = phase == 2 ? READ_BUSY : BASELINE;
        pid_t owner = start_owner(dir, fixture);
        volatile pid_t *slot = phase == 0 ? &fixture->operation_pid :
            phase == 1 ? &fixture->recovery_pid : &fixture->reader_pid;
        wait_for_pid(slot);
        pid_t worker = *slot;
        assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
        kill_owner(owner);
        wait_for_absence(worker);
        assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
        fixture->recovery = EXIT_OK;
        fixture->reading = phase == 2 && full ? BASELINE : CHANGED;
        ControlLease lease = held_lease();
        lease.recovery_verified = true; // A stale memory proof must be discarded.
        uint64_t started = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
        WorkerSupervisorReport report = resume_when_available(dir, &lease, fixture);
        assert(report.operation_pid == 0 && report.operation == WORKER_NOT_STARTED);
        assert(report.reaped == 2 && fixture->operations == 1);
        assert(!lease.recovery_verified);
        if (phase == 2 && full) {
            assert(report.result == SUPERVISOR_RECOVERED);
            assert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - started >= CONTROL_LEASE_BASELINE_NS);
            assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR);
            assert(lease.samples == CONTROL_LEASE_REQUIRED_SAMPLES);
        } else {
            assert(report.result == SUPERVISOR_OBSERVATION_FAILED);
            assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
        }
        fixture_free(fixture, dir);
    }
}

static void stopped_or_recorded_live_worker_blocks_a_new_owner(void) {
    int dir;
    Fixture *fixture = fixture_new(&dir);
    fixture->operation = STOPPED;
    pid_t owner = start_owner(dir, fixture);
    wait_for_pid(&fixture->operation_pid);
    // Wait until SIGSTOP has actually stopped both callback and monitor threads.
    uint64_t deadline = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) + 5 * SECOND;
    for (;;) {
        struct proc_bsdinfo info = {0};
        int size = proc_pidinfo(fixture->operation_pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
        if (size == sizeof(info) && info.pbi_status == SSTOP) break;
        assert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) < deadline);
        struct timespec delay = {.tv_nsec = 10000000};
        (void)nanosleep(&delay, NULL);
    }
    kill_owner(owner);
    ControlLease lease = held_lease();
    lease.recovery_verified = true;
    WorkerSupervisorReport report = resume(dir, &lease, fixture);
    assert(report.result == SUPERVISOR_BLOCKED && fixture->recoveries == 0);
    assert(!lease.recovery_verified && lease.state == CONTROL_LEASE_RECOVERY_REQUIRED);
    // Test-only intervention on the exact still-stopped descendant we created.
    assert(kill(fixture->operation_pid, SIGCONT) == 0);
    wait_for_absence(fixture->operation_pid);
    fixture->reading = CHANGED;
    report = resume_when_available(dir, &lease, fixture);
    assert(report.result == SUPERVISOR_OBSERVATION_FAILED && fixture->recoveries == 1);
    fixture_free(fixture, dir);

    fixture = fixture_new(&dir);
    lease = held_lease();
    assert(control_intent_mark_pending(dir, &lease));
    assert(supervisor_ownership_record(dir, getpid()));
    // A free flock is insufficient: the stored PID still exists, including reuse.
    report = resume(dir, &lease, fixture);
    assert(report.result == SUPERVISOR_BLOCKED && fixture->recoveries == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
    fixture_free(fixture, dir);
}

static void missing_or_malformed_worker_record_never_allows_restart(void) {
    int dir;
    Fixture *fixture = fixture_new(&dir);
    ControlLease lease = held_lease();
    assert(control_intent_mark_pending(dir, &lease));
    // A stale pre-lock CLEAR must not overwrite an incomplete worker record.
    assert(supervisor_ownership_acquire(dir, false) < 0);
    assert(faccessat(dir, "supervisor-worker-v1", F_OK, 0) < 0 && errno == ENOENT);
    WorkerSupervisorReport report = resume(dir, &lease, fixture);
    assert(report.result == SUPERVISOR_BLOCKED && fixture->recoveries == 0);
    int bad = openat(dir, "supervisor-worker-v1", O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(bad >= 0 && write(bad, "bad", 3) == 3 && close(bad) == 0);
    report = resume(dir, &lease, fixture);
    assert(report.result == SUPERVISOR_BLOCKED && fixture->recoveries == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
    fixture_free(fixture, dir);
}

static void unsafe_ownership_files_never_allow_a_callback(void) {
    int dir;
    Fixture *fixture = fixture_new(&dir);
    ControlLease lease = held_lease();
    assert(control_intent_mark_pending(dir, &lease));
    assert(supervisor_ownership_record(dir, 0));
    int lock = supervisor_ownership_acquire(dir, true);
    assert(lock >= 0 && close(lock) == 0);
    assert(fchmodat(dir, "supervisor-owner-v1", 0644, 0) == 0);
    assert(resume(dir, &lease, fixture).result == SUPERVISOR_BLOCKED);
    assert(fchmodat(dir, "supervisor-owner-v1", 0600, 0) == 0);
    assert(fchmodat(dir, "supervisor-worker-v1", 0644, 0) == 0);
    assert(resume(dir, &lease, fixture).result == SUPERVISOR_BLOCKED);
    assert(fchmodat(dir, "supervisor-worker-v1", 0600, 0) == 0);
    assert(linkat(dir, "supervisor-worker-v1", dir, "record-alias", 0) == 0);
    assert(resume(dir, &lease, fixture).result == SUPERVISOR_BLOCKED);
    assert(unlinkat(dir, "record-alias", 0) == 0);
    assert(renameat(dir, "supervisor-worker-v1", dir, "record-backup") == 0);
    assert(symlinkat("record-backup", dir, "supervisor-worker-v1") == 0);
    assert(resume(dir, &lease, fixture).result == SUPERVISOR_BLOCKED);
    assert(unlinkat(dir, "supervisor-worker-v1", 0) == 0);
    assert(renameat(dir, "record-backup", dir, "supervisor-worker-v1") == 0);
    assert(fchmod(dir, 0755) == 0);
    assert(resume(dir, &lease, fixture).result == SUPERVISOR_BLOCKED);
    assert(fchmod(dir, 0700) == 0);
    assert(fixture->operations == 0 && fixture->recoveries == 0 && fixture->reads == 0);
    assert(control_intent_read(dir) == CONTROL_INTENT_PENDING);
    fixture_free(fixture, dir);
}

static void initial_baseline_is_bounded_and_never_recovery_proof(bool full) {
    const Reading readings[] = {BASELINE, CHANGED, READ_FAILED, READ_STOPPED};
    for (unsigned index = full ? 0 : 1; index < sizeof(readings) / sizeof(readings[0]); ++index) {
        int dir;
        Fixture *fixture = fixture_new(&dir);
        fixture->reading = readings[index];
        ControlLease lease = held_lease();
        SmcBaselineSnapshot latest = {0};
        WorkerSupervisorBackend backend = {.context = fixture, .recover = recover, .read = read_snapshot};
        uint64_t started = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
        WorkerSupervisorReport report = worker_supervisor_observe_baseline(dir, &lease, &latest, backend, limits());
        uint64_t duration = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - started;
        assert(report.operation_pid == 0 && report.recovery_pid == 0 && report.reaped == 1);
        assert_reaped(report.observer_pid);
        assert(fixture->operations == 0 && fixture->recoveries == 0);
        assert(control_intent_read(dir) == CONTROL_INTENT_CLEAR && !lease.recovery_verified);
        assert(!control_lease_take_recovery_proof(&lease));
        if (readings[index] == BASELINE) {
            assert(report.result == SUPERVISOR_BASELINE_READY && lease.state == CONTROL_LEASE_STABLE);
            assert(duration >= CONTROL_LEASE_BASELINE_NS && duration < 75 * SECOND);
            assert(lease.samples == 61 && smc_baseline_is_system(&latest));
            assert(control_lease_claim(&lease, 7, clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW), true,
                                       CONTROL_INTENT_CLEAR, SMC_BASELINE_OK, &latest));
        } else {
            assert(report.result == SUPERVISOR_OBSERVATION_FAILED);
            assert(lease.state != CONTROL_LEASE_STABLE && duration < 10 * SECOND);
        }
        fixture_free(fixture, dir);
    }
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
    owner_crash_stops_each_phase_and_restart_uses_a_fresh_window(argc == 1);
    stopped_or_recorded_live_worker_blocks_a_new_owner();
    missing_or_malformed_worker_record_never_allows_restart();
    unsafe_ownership_files_never_allow_a_callback();
    initial_baseline_is_bounded_and_never_recovery_proof(argc == 1);
    puts("worker supervisor process tests passed (no SMC access)");
    return 0;
}
