#include "SupervisorProbeStorage.h"
#include "SupervisorOwnership.h"
#include "ControlLease.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void private_state_and_exclusive_lock_are_required(int directory) {
    assert(fchmod(directory, 0755) == 0);
    assert(supervisor_probe_lock(directory) < 0);
    assert(fchmod(directory, 0700) == 0);
    int lock = supervisor_probe_lock(directory);
    assert(lock >= 0);
    assert(supervisor_probe_lock(directory) < 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) { close(lock); _exit(supervisor_probe_lock(directory) < 0 ? 0 : 1); }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(close(lock) == 0);
    assert(fchmodat(directory, "probe-launch-v1", 0644, 0) == 0);
    assert(supervisor_probe_lock(directory) < 0);
    assert(fchmodat(directory, "probe-launch-v1", 0600, 0) == 0);
    assert(linkat(directory, "probe-launch-v1", directory, "alias", 0) == 0);
    assert(supervisor_probe_lock(directory) < 0);
    assert(unlinkat(directory, "alias", 0) == 0);
    assert(renameat(directory, "probe-launch-v1", directory, "backup") == 0);
    assert(symlinkat("backup", directory, "probe-launch-v1") == 0);
    assert(supervisor_probe_lock(directory) < 0);
    assert(unlinkat(directory, "probe-launch-v1", 0) == 0);
    assert(renameat(directory, "backup", directory, "probe-launch-v1") == 0);
}

static void pending_or_recorded_process_prohibits_cleanup(int directory) {
    int lock = supervisor_probe_lock(directory);
    assert(lock >= 0);
    assert(supervisor_ownership_record(directory, getpid()));
    assert(!supervisor_probe_cleanup(directory));
    assert(supervisor_ownership_record(directory, 0));
    ControlLease lease = {.state = CONTROL_LEASE_HELD, .owner = 1};
    assert(control_intent_mark_pending(directory, &lease));
    assert(!supervisor_probe_cleanup(directory));
    assert(control_intent_read(directory) == CONTROL_INTENT_PENDING);
    // Test teardown only, never used by the executable.
    assert(unlinkat(directory, "control-intent-v1", 0) == 0);
    assert(supervisor_probe_cleanup(directory));
    assert(faccessat(directory, "supervisor-worker-v1", F_OK, 0) != 0 && errno == ENOENT);
    assert(close(lock) == 0);
}

int main(void) {
    char path[] = "/private/tmp/ventilator-probe-storage-test.XXXXXX";
    assert(mkdtemp(path));
    int directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    assert(directory >= 0);
    private_state_and_exclusive_lock_are_required(directory);
    pending_or_recorded_process_prohibits_cleanup(directory);
    assert(unlinkat(directory, "probe-launch-v1", 0) == 0);
    assert(close(directory) == 0 && rmdir(path) == 0);
    puts("read-only supervisor storage tests passed");
}
