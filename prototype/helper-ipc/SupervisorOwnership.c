#include "SupervisorOwnership.h"
#include "ControlIntentJournal.h"

#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char lock_name[] = "supervisor-owner-v1";
static const char worker_name[] = "supervisor-worker-v1";
static const char next_name[] = "supervisor-worker.next";

static bool private_directory(int directory_fd) {
    struct stat info = {0};
    return directory_fd >= 0 && fstat(directory_fd, &info) == 0 &&
           S_ISDIR(info.st_mode) && info.st_uid == geteuid() && (info.st_mode & 0077) == 0;
}

static bool private_file(int descriptor) {
    struct stat info = {0};
    return descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
           info.st_uid == geteuid() && (info.st_mode & 0777) == 0600 && info.st_nlink == 1;
}

static bool old_worker_absent(int directory_fd, bool pending) {
    int descriptor = openat(directory_fd, worker_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0) return errno == ENOENT && !pending;
    char contents[64] = {0};
    ssize_t count = private_file(descriptor) ? read(descriptor, contents, sizeof(contents) - 1) : -1;
    struct stat info = {0};
    bool exact = count > 0 && fstat(descriptor, &info) == 0 && info.st_size == count;
    if (close(descriptor) != 0) return false;
    long recorded = -1;
    int used = 0;
    if (!exact || sscanf(contents, "Ventilator worker v1 %ld\n%n", &recorded, &used) != 1 ||
        used != count || recorded < 0 || recorded > INT_MAX) return false;
    // Zero is a durable statement made before any child is granted execution.
    if (recorded == 0) return true;
    struct proc_bsdinfo process = {0};
    errno = 0;
    int size = proc_pidinfo((int)recorded, PROC_PIDTBSDINFO, 0, &process, sizeof(process));
    // Any live/reused PID blocks; this protocol never sends it a signal.
    // Permission errors, partial replies and zombies also fail closed.
    return size == 0 && errno == ESRCH;
}

bool supervisor_ownership_record(int directory_fd, pid_t child) {
    if (!private_directory(directory_fd) || child < 0) return false;
    char contents[64];
    int length = snprintf(contents, sizeof(contents), "Ventilator worker v1 %ld\n", (long)child);
    if (length <= 0 || length >= (int)sizeof(contents)) return false;
    int descriptor = openat(directory_fd, next_name,
                             O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!private_file(descriptor)) {
        if (descriptor >= 0) (void)close(descriptor);
        return false;
    }
    bool saved = ftruncate(descriptor, 0) == 0;
    size_t written = 0;
    while (saved && written < (size_t)length) {
        ssize_t count = write(descriptor, contents + written, (size_t)length - written);
        if (count > 0) written += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else saved = false;
    }
    saved = saved && fsync(descriptor) == 0;
    if (close(descriptor) != 0) saved = false;
    return saved && renameat(directory_fd, next_name, directory_fd, worker_name) == 0 &&
           fsync(directory_fd) == 0;
}

int supervisor_ownership_acquire(int directory_fd, bool pending) {
    if (!private_directory(directory_fd)) return -1;
    int descriptor = openat(directory_fd, lock_name,
                             O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!private_file(descriptor) || flock(descriptor, LOCK_EX | LOCK_NB) != 0 ||
        control_intent_read(directory_fd) != (pending ? CONTROL_INTENT_PENDING : CONTROL_INTENT_CLEAR) ||
        !old_worker_absent(directory_fd, pending)) {
        if (descriptor >= 0) (void)close(descriptor);
        return -1;
    }
    struct stat opened = {0}, named = {0};
    if (fstat(descriptor, &opened) != 0 ||
        fstatat(directory_fd, lock_name, &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        opened.st_dev != named.st_dev || opened.st_ino != named.st_ino ||
        !supervisor_ownership_record(directory_fd, 0)) {
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static void *parent_connection_watch(void *context) {
    int connection = (int)(intptr_t)context;
    char unexpected;
    ssize_t count;
    do {
        count = read(connection, &unexpected, 1);
    } while (count < 0 && errno == EINTR);
    // EOF, unexpected data, or a socket error invalidates this worker's owner.
    _exit(125);
}

void supervisor_ownership_enter_child(int connection, uint64_t deadline_ns) {
    char grant = 0;
    ssize_t count;
    do {
        count = read(connection, &grant, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1 || grant != 'G' ||
        clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) >= deadline_ns) _exit(126);
    pthread_t watcher;
    if (pthread_create(&watcher, NULL, parent_connection_watch,
                        (void *)(intptr_t)connection) != 0 ||
        pthread_detach(watcher) != 0) _exit(126);
}
