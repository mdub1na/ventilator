#include "WorkerSupervisor.h"
#include "SupervisorOwnership.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const uint64_t SECOND = UINT64_C(1000000000);
static const uint64_t MAX_READ_NS = UINT64_C(5000000000);

typedef struct {
    uint64_t started_ns;
    uint64_t finished_ns;
    SmcBaselineResult result;
    SmcBaselineSnapshot snapshot;
} Observation;

_Static_assert(sizeof(Observation) <= PIPE_BUF, "one observation must fit atomically");

static uint64_t now_ns(void) {
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}

static void parent_check(WorkerSupervisorBackend backend) {
    if (backend.parent_check != NULL) backend.parent_check(backend.context);
}

static bool deadline_for(uint64_t duration, uint64_t *deadline) {
    uint64_t now = now_ns();
    if (now == 0 || duration == 0 || now > UINT64_MAX - duration) return false;
    *deadline = now + duration;
    return true;
}

static void pause_ns(uint64_t duration) {
    struct timespec delay = {
        .tv_sec = (time_t)(duration / SECOND),
        .tv_nsec = (long)(duration % SECOND),
    };
    // An interrupted pause returns to the deadline check instead of extending it.
    (void)nanosleep(&delay, NULL);
}

static bool limits_valid(WorkerSupervisorLimits limits) {
    return limits.operation_ns > 0 && limits.operation_ns <= CONTROL_LEASE_TTL_NS &&
           limits.recovery_ns > 0 && limits.recovery_ns <= 90 * SECOND &&
           limits.observation_ns >= CONTROL_LEASE_BASELINE_NS &&
           limits.observation_ns <= 90 * SECOND &&
           limits.reap_ns > 0 && limits.reap_ns <= 5 * SECOND;
}

static WorkerExit classify_exit(int status) {
    if (WIFSIGNALED(status)) return WORKER_CRASHED;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? WORKER_EXITED : WORKER_FAILED;
}

static bool stop_and_reap(pid_t child, uint64_t reap_ns,
                           WorkerSupervisorReport *report) {
    uint64_t deadline = 0;
    if (child <= 0 || !deadline_for(reap_ns, &deadline)) return false;
    int status = 0;
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child) {
        ++report->reaped;
        return true;
    }
    if (waited < 0 && errno != EINTR) return false;
    if (kill(child, SIGKILL) != 0 && errno != ESRCH) return false;
    for (;;) {
        int status = 0;
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            ++report->reaped;
            return true;
        }
        // ECHILD is not proof that THIS supervisor reaped its worker.
        if (waited < 0 && errno != EINTR) return false;
        uint64_t now = now_ns();
        if (now == 0 || now >= deadline) return false;
        pause_ns(UINT64_C(10000000));
    }
}

static bool wait_phase(pid_t child, uint64_t deadline, uint64_t reap_ns,
                        const volatile sig_atomic_t *cancel, WorkerExit *exit,
                        WorkerSupervisorReport *report, WorkerSupervisorBackend backend) {
    for (;;) {
        parent_check(backend);
        uint64_t now = now_ns();
        // A process exiting while the supervisor slept cannot pass a deadline.
        if (now == 0 || now >= deadline || (cancel != NULL && *cancel != 0)) {
            *exit = cancel != NULL && *cancel != 0 ? WORKER_CANCELLED : WORKER_TIMED_OUT;
            return stop_and_reap(child, reap_ns, report);
        }
        int status = 0;
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            ++report->reaped;
            *exit = classify_exit(status);
            return true;
        }
        if (waited < 0 && errno != EINTR) return false;
        pause_ns(UINT64_C(10000000));
    }
}

static pid_t spawn_worker(int directory_fd, uint64_t deadline, int *connection,
                           WorkerSupervisorBackend backend) {
    parent_check(backend);
    int channel[2];
    *connection = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, channel) != 0) return -1;
    if (fcntl(channel[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(channel[1], F_SETFD, FD_CLOEXEC) != 0) {
        (void)close(channel[0]);
        (void)close(channel[1]);
        return -1;
    }
    pid_t child = fork();
    if (child == 0) {
        (void)close(channel[0]);
        (void)close(directory_fd);
        // Only the single-threaded parent may hold the daemon's report pipe.
        // Otherwise a blocked worker can keep NSTask's stdout open after EOF.
        (void)close(STDOUT_FILENO);
        supervisor_ownership_enter_child(channel[1], deadline);
        return 0;
    }
    (void)close(channel[1]);
    if (child < 0) {
        (void)close(channel[0]);
        return child;
    }
    // No callback can begin before its PID has been durably recorded.
    bool recorded = supervisor_ownership_record(directory_fd, child);
    parent_check(backend);
    if (!recorded || send(channel[0], "G", 1, MSG_NOSIGNAL) != 1) {
        (void)close(channel[0]);
    } else {
        *connection = channel[0];
    }
    return child;
}

static pid_t start_action(int directory_fd, int (*action)(void *), WorkerSupervisorBackend backend,
                           uint64_t deadline, int *connection) {
    pid_t child = spawn_worker(directory_fd, deadline, connection, backend);
    if (child == 0) _exit(action(backend.context) == 0 ? 0 : 1);
    return child;
}

static void observe_child(int output, WorkerSupervisorBackend backend) {
    for (unsigned index = 0; index < CONTROL_LEASE_REQUIRED_SAMPLES; ++index) {
        Observation observation = {0};
        observation.started_ns = now_ns();
        observation.result = backend.read(backend.context, &observation.snapshot);
        observation.finished_ns = now_ns();
        ssize_t count;
        do {
            count = write(output, &observation, sizeof(observation));
        } while (count < 0 && errno == EINTR);
        if (count != (ssize_t)sizeof(observation)) _exit(1);
        if (observation.result != SMC_BASELINE_OK) _exit(1);
        if (index + 1 < CONTROL_LEASE_REQUIRED_SAMPLES) pause_ns(SECOND);
    }
    _exit(0);
}

static bool accept_observation(ControlLease *lease, const Observation *observation,
                                uint64_t launched_ns, uint64_t received_ns,
                                unsigned samples, bool recovering) {
    if (observation->started_ns < launched_ns ||
        observation->finished_ns < observation->started_ns ||
        observation->finished_ns - observation->started_ns > MAX_READ_NS ||
        received_ns < observation->finished_ns ||
        received_ns - observation->finished_ns > 2 * SECOND ||
        (samples > 0 && observation->started_ns < lease->last_sample_ns)) return false;
    if (samples == 0) {
        if (recovering) control_lease_recovery_started(lease, observation->result,
                                       &observation->snapshot, observation->finished_ns);
        else control_lease_start(lease, CONTROL_INTENT_CLEAR, observation->result,
                                  &observation->snapshot, observation->finished_ns);
    } else {
        control_lease_sample(lease, observation->result,
                             &observation->snapshot, observation->finished_ns);
    }
    return lease->state == (recovering ? CONTROL_LEASE_VERIFYING_RECOVERY : CONTROL_LEASE_OBSERVING) ||
           (lease->state == CONTROL_LEASE_STABLE && lease->recovery_verified == recovering);
}

static bool supervise_observer(int input, pid_t child, ControlLease *lease,
                                uint64_t launched_ns, uint64_t deadline,
                                uint64_t reap_ns, WorkerSupervisorReport *report,
                                bool recovering, SmcBaselineSnapshot *latest,
                                WorkerSupervisorBackend backend) {
    Observation observation = {0};
    size_t filled = 0;
    unsigned samples = 0;
    bool eof = false;
    bool reaped = false;
    bool exited_ok = false;
    uint64_t progress_deadline = launched_ns + MAX_READ_NS + 2 * SECOND;
    for (;;) {
        parent_check(backend);
        uint64_t now = now_ns();
        if (now == 0 || now >= deadline || now >= progress_deadline) break;
        if (!eof) {
            ssize_t count = read(input, (char *)&observation + filled,
                                 sizeof(observation) - filled);
            if (count > 0) {
                filled += (size_t)count;
                if (filled == sizeof(observation)) {
                    if (samples >= CONTROL_LEASE_REQUIRED_SAMPLES ||
                        !accept_observation(lease, &observation, launched_ns,
                                            now_ns(), samples, recovering)) break;
                    if (latest != NULL) *latest = observation.snapshot;
                    ++samples;
                    progress_deadline = observation.finished_ns + MAX_READ_NS + 2 * SECOND;
                    filled = 0;
                }
                continue;
            }
            if (count == 0) eof = true;
            else if (errno != EAGAIN && errno != EINTR) break;
        }
        if (!reaped) {
            int status = 0;
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) {
                reaped = true;
                ++report->reaped;
                exited_ok = classify_exit(status) == WORKER_EXITED;
            } else if (waited < 0 && errno != EINTR) {
                report->result = SUPERVISOR_STOP_FAILED;
                return false;
            }
        }
        if (reaped && eof) {
            return exited_ok && filled == 0 &&
                   samples == CONTROL_LEASE_REQUIRED_SAMPLES &&
                   lease->state == CONTROL_LEASE_STABLE && lease->recovery_verified == recovering;
        }
        pause_ns(UINT64_C(10000000));
    }
    if (!reaped && !stop_and_reap(child, reap_ns, report)) {
        report->result = SUPERVISOR_STOP_FAILED;
    }
    return false;
}

static bool can_supervise(WorkerSupervisorBackend backend, WorkerSupervisorLimits limits) {
    struct sigaction child_action = {0};
    return limits_valid(limits) && backend.recover != NULL && backend.read != NULL &&
           sigaction(SIGCHLD, NULL, &child_action) == 0 &&
           child_action.sa_handler == SIG_DFL && (child_action.sa_flags & SA_NOCLDWAIT) == 0;
}

static bool observe_owned(
    int directory_fd, ControlLease *lease, WorkerSupervisorBackend backend,
    WorkerSupervisorLimits limits, WorkerSupervisorReport *report,
    bool recovering, SmcBaselineSnapshot *latest) {
    int connection = -1;
    uint64_t deadline = 0;
    int channel[2];
    if (pipe(channel) != 0) return false;
    if (fcntl(channel[0], F_SETFL, O_NONBLOCK) != 0) {
        (void)close(channel[0]);
        (void)close(channel[1]);
        return false;
    }
    uint64_t launched_ns = now_ns();
    if (!deadline_for(limits.observation_ns, &deadline)) {
        (void)close(channel[0]);
        (void)close(channel[1]);
        return false;
    }
    report->observer_pid = spawn_worker(directory_fd, deadline, &connection, backend);
    if (report->observer_pid == 0) {
        (void)close(channel[0]);
        observe_child(channel[1], backend);
    }
    (void)close(channel[1]);
    if (connection >= 0 && report->observer_pid > 0 && backend.observer_started != NULL)
        backend.observer_started(backend.context, report->observer_pid, recovering);
    bool verified = report->observer_pid > 0 &&
        supervise_observer(channel[0], report->observer_pid, lease,
                            launched_ns, deadline, limits.reap_ns, report, recovering, latest, backend);
    (void)close(channel[0]);
    if (connection >= 0) (void)close(connection);
    return verified;
}

WorkerSupervisorReport worker_supervisor_observe_baseline(
    int directory_fd, ControlLease *lease, SmcBaselineSnapshot *latest,
    WorkerSupervisorBackend backend, WorkerSupervisorLimits limits) {
    WorkerSupervisorReport report = {.result = SUPERVISOR_BLOCKED};
    parent_check(backend);
    if (lease == NULL || latest == NULL || !can_supervise(backend, limits) ||
        control_intent_read(directory_fd) != CONTROL_INTENT_CLEAR) return report;
    int owner_lock = supervisor_ownership_acquire(directory_fd, false);
    if (owner_lock < 0) return report;
    memset(lease, 0, sizeof(*lease));
    report.result = SUPERVISOR_OBSERVATION_FAILED;
    if (observe_owned(directory_fd, lease, backend, limits, &report, false, latest))
        report.result = SUPERVISOR_BASELINE_READY;
    else control_lease_sleep(lease);
    (void)close(owner_lock);
    return report;
}

static WorkerSupervisorReport recover_owned(
    int directory_fd, ControlLease *lease, WorkerSupervisorBackend backend,
    WorkerSupervisorLimits limits, WorkerSupervisorReport report) {
    uint64_t deadline = 0;
    int connection = -1;
    report.result = SUPERVISOR_RECOVERY_FAILED;
    if (!deadline_for(limits.recovery_ns, &deadline)) return report;
    report.recovery_pid = start_action(directory_fd, backend.recover,
                                        backend, deadline, &connection);
    if (report.recovery_pid <= 0) return report;
    WorkerExit recovery_exit = WORKER_NOT_STARTED;
    bool stopped = wait_phase(report.recovery_pid, deadline, limits.reap_ns, NULL,
                               &recovery_exit, &report, backend);
    if (connection >= 0) (void)close(connection);
    if (!stopped) {
        report.result = SUPERVISOR_STOP_FAILED;
        return report;
    }
    if (recovery_exit != WORKER_EXITED) return report;

    report.result = SUPERVISOR_OBSERVATION_FAILED;
    bool verified = observe_owned(directory_fd, lease, backend, limits, &report, true, NULL);
    parent_check(backend);
    if (verified && control_intent_clear_verified(directory_fd, lease)) {
        report.result = SUPERVISOR_RECOVERED;
    } else {
        // An uncollected observer must not leave a consumable proof in memory.
        control_lease_sleep(lease);
    }
    return report;
}

static WorkerSupervisorReport run_owned(
    int directory_fd, ControlLease *lease, WorkerSupervisorBackend backend,
    WorkerSupervisorLimits limits, const volatile sig_atomic_t *cancel) {
    WorkerSupervisorReport report = {.result = SUPERVISOR_BLOCKED};
    uint64_t deadline = 0;
    uint64_t owner = lease->owner;
    parent_check(backend);
    if (!control_intent_mark_pending(directory_fd, lease) ||
        !control_lease_mark_write_pending(lease, owner, now_ns(), CONTROL_INTENT_PENDING)) {
        control_lease_owner_lost(lease, owner);
        return report;
    }
    if (deadline_for(limits.operation_ns, &deadline)) {
        if (deadline > lease->expires_at_ns) deadline = lease->expires_at_ns;
        int connection = -1;
        report.operation_pid = start_action(directory_fd, backend.operate,
                                             backend, deadline, &connection);
        bool stopped = report.operation_pid <= 0 ||
            wait_phase(report.operation_pid, deadline, limits.reap_ns, cancel,
                        &report.operation, &report, backend);
        if (connection >= 0) (void)close(connection);
        if (!stopped) {
            control_lease_owner_lost(lease, owner);
            report.result = SUPERVISOR_STOP_FAILED;
            return report;
        }
    }
    control_lease_owner_lost(lease, owner);
    return recover_owned(directory_fd, lease, backend, limits, report);
}

WorkerSupervisorReport worker_supervisor_run(
    int directory_fd, ControlLease *lease, WorkerSupervisorBackend backend,
    WorkerSupervisorLimits limits, const volatile sig_atomic_t *cancel) {
    WorkerSupervisorReport blocked = {.result = SUPERVISOR_BLOCKED};
    parent_check(backend);
    uint64_t now = now_ns();
    if (lease == NULL || !can_supervise(backend, limits) || backend.operate == NULL ||
        (cancel != NULL && *cancel != 0) || lease->state != CONTROL_LEASE_HELD ||
        lease->owner == 0 || now < lease->last_sample_ns || now >= lease->expires_at_ns ||
        now - lease->last_sample_ns > 2 * SECOND ||
        control_intent_read(directory_fd) != CONTROL_INTENT_CLEAR) return blocked;
    int owner_lock = supervisor_ownership_acquire(directory_fd, false);
    if (owner_lock < 0) return blocked;
    WorkerSupervisorReport report = run_owned(directory_fd, lease, backend, limits, cancel);
    (void)close(owner_lock); // Workers keep their reference until actual exit.
    return report;
}

WorkerSupervisorReport worker_supervisor_resume(
    int directory_fd, ControlLease *lease, WorkerSupervisorBackend backend,
    WorkerSupervisorLimits limits) {
    WorkerSupervisorReport blocked = {.result = SUPERVISOR_BLOCKED};
    parent_check(backend);
    if (lease == NULL || !can_supervise(backend, limits) ||
        control_intent_read(directory_fd) != CONTROL_INTENT_PENDING) return blocked;
    control_lease_start(lease, CONTROL_INTENT_PENDING, SMC_BASELINE_READ_FAILED, NULL, now_ns());
    int owner_lock = supervisor_ownership_acquire(directory_fd, true);
    if (owner_lock < 0) return blocked;
    WorkerSupervisorReport report = recover_owned(directory_fd, lease, backend, limits, blocked);
    (void)close(owner_lock);
    return report;
}
