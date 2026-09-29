#ifndef VENTILATOR_WORKER_SUPERVISOR_H
#define VENTILATOR_WORKER_SUPERVISOR_H

#include "ControlLease.h"

#include <signal.h>
#include <sys/types.h>

typedef struct {
    uint64_t operation_ns;   // At most the lease TTL (15 seconds).
    uint64_t recovery_ns;    // At most 90 seconds, independently bounded.
    uint64_t observation_ns; // 60–90 seconds for a fresh baseline window.
    uint64_t reap_ns;        // At most 5 seconds after SIGKILL.
} WorkerSupervisorLimits;

// Trusted internal callbacks, never executable paths, commands or client PIDs.
// Each runs in its own child of a SINGLE-THREADED process. This prototype must
// not be called from the current multithreaded XPC daemon.
// Callbacks must not spawn descendants or reap children; SIGCHLD must be default.
typedef struct {
    void *context;
    int (*operate)(void *context);
    int (*recover)(void *context);
    SmcBaselineResult (*read)(void *context, SmcBaselineSnapshot *snapshot);
} WorkerSupervisorBackend;

typedef enum {
    WORKER_NOT_STARTED,
    WORKER_EXITED,
    WORKER_FAILED,
    WORKER_CRASHED,
    WORKER_TIMED_OUT,
    WORKER_CANCELLED,
} WorkerExit;

typedef enum {
    SUPERVISOR_BLOCKED,
    SUPERVISOR_RECOVERED,
    SUPERVISOR_RECOVERY_FAILED,
    SUPERVISOR_OBSERVATION_FAILED,
    SUPERVISOR_STOP_FAILED,
} WorkerSupervisorResult;

typedef struct {
    WorkerSupervisorResult result;
    WorkerExit operation;
    pid_t operation_pid;
    pid_t recovery_pid;
    pid_t observer_pid;
    unsigned reaped;
} WorkerSupervisorReport;

// Saves the persistent intent BEFORE forking. Stops/reaps each owned child
// before the next phase. Only the parent validates timed snapshots and consumes
// the journal's recovery proof. Failure retains the pending marker. A caller
// must also prohibit new work after STOP_FAILED (the child may still be alive).
// The parent itself is not protected against SIGKILL or kernel/power failure.
WorkerSupervisorReport worker_supervisor_run(
    int directory_fd, ControlLease *lease, WorkerSupervisorBackend backend,
    WorkerSupervisorLimits limits, const volatile sig_atomic_t *cancel);

#endif
