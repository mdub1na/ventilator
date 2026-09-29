#include "SupervisorProbeCrash.h"
#include "ControlIntentJournal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <libproc.h>
#include <time.h>
#include <unistd.h>

bool supervisor_probe_crash_after_observer_start(int directory_fd, pid_t observer) {
    struct proc_bsdinfo child = {0};
    if (observer <= 1 || observer == getpid() ||
        proc_pidinfo(observer, PROC_PIDTBSDINFO, 0, &child, sizeof(child)) != sizeof(child) ||
        child.pbi_ppid != (uint32_t)getpid() || child.pbi_uid != geteuid() ||
        control_intent_read(directory_fd) != CONTROL_INTENT_PENDING) return false;
    uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
    if (now == 0) return false;
    char message[256];
    int length = snprintf(message, sizeof(message),
        "{\"runner_pid\":%ld,\"runner_uid\":%u,\"observer_pid\":%ld,"
        "\"journal_pending\":true,\"armed_monotonic_ns\":%llu}\n",
        (long)getpid(), (unsigned)geteuid(), (long)observer, (unsigned long long)now);
    if (length <= 0 || length >= (int)sizeof(message)) return false;
    ssize_t sent;
    do { sent = write(STDOUT_FILENO, message, (size_t)length); } while (sent < 0 && errno == EINTR);
    if (sent != length) return false;
    (void)raise(SIGKILL);
    return false;
}
