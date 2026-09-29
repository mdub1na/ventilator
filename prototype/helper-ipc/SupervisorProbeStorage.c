#include <stdbool.h>
#include "SupervisorProbeStorage.h"
#include "SupervisorOwnership.h"
#include "SupervisorProbeDirectory.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

int supervisor_probe_lock(int directory_fd) {
    struct stat directory = {0}, file = {0}, named = {0};
    if (fstat(directory_fd, &directory) != 0 || !S_ISDIR(directory.st_mode) ||
        directory.st_uid != geteuid() || (directory.st_mode & 0777) != 0700) return -1;
    int lock = openat(directory_fd, "probe-launch-v1", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (lock < 0) return -1;
    if (fstat(lock, &file) != 0 || !S_ISREG(file.st_mode) || file.st_uid != geteuid() ||
        (file.st_mode & 0777) != 0600 || file.st_nlink != 1 ||
        flock(lock, LOCK_EX | LOCK_NB) != 0 ||
        fstatat(directory_fd, "probe-launch-v1", &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        file.st_dev != named.st_dev || file.st_ino != named.st_ino) {
        (void)close(lock);
        return -1;
    }
    return lock;
}

bool supervisor_probe_cleanup(int directory_fd) {
    // Called with the launch lock held. Reuses the durable PID guard; a live
    // worker, pending intent or uncertain record prohibits removing state.
    int ownership = supervisor_ownership_acquire(directory_fd, false);
    if (ownership < 0) return false;
    const char *files[] = {"supervisor-worker.next", "supervisor-worker-v1", "supervisor-owner-v1",
                          SupervisorProbeExecutableName, SupervisorProbeStagingName};
    bool removed = true;
    for (unsigned index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
        if (unlinkat(directory_fd, files[index], 0) != 0 && errno != ENOENT) removed = false;
    }
    if (fsync(directory_fd) != 0) removed = false;
    (void)close(ownership);
    return removed;
}
