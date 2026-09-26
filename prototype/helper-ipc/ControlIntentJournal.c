#include "ControlIntentJournal.h"
#include "ControlLease.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char marker_name[] = "control-intent-v1";
static const char marker_contents[] = "Ventilator control pending: Mac15,7 macOS 27.0 v1\n";

static bool private_directory(int directory_fd) {
    struct stat info = {0};
    return directory_fd >= 0 && fstat(directory_fd, &info) == 0 &&
           S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
           (info.st_mode & 0077) == 0;
}

ControlIntentStatus control_intent_read(int directory_fd) {
    if (!private_directory(directory_fd)) return CONTROL_INTENT_UNKNOWN;
    int marker = openat(directory_fd, marker_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (marker < 0) return errno == ENOENT ? CONTROL_INTENT_CLEAR : CONTROL_INTENT_UNKNOWN;
    struct stat info = {0};
    ControlIntentStatus result = CONTROL_INTENT_UNKNOWN;
    if (fstat(marker, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_uid == geteuid() && (info.st_mode & 0777) == 0600 &&
        info.st_nlink == 1 && info.st_size == (off_t)(sizeof(marker_contents) - 1)) {
        char contents[sizeof(marker_contents)] = {0};
        size_t filled = 0;
        while (filled < sizeof(marker_contents) - 1) {
            ssize_t count = read(marker, contents + filled,
                                 sizeof(marker_contents) - 1 - filled);
            if (count > 0) filled += (size_t)count;
            else if (count == 0) break;
            else if (errno != EINTR) break;
        }
        if (filled == sizeof(marker_contents) - 1 &&
            memcmp(contents, marker_contents, filled) == 0) {
            result = CONTROL_INTENT_PENDING;
        }
    }
    if (close(marker) != 0) return CONTROL_INTENT_UNKNOWN;
    return result;
}

bool control_intent_mark_pending(int directory_fd, struct ControlLease *lease) {
    if (!control_lease_prepare_persistent_intent(lease)) return false;
    if (control_intent_read(directory_fd) != CONTROL_INTENT_CLEAR) return false;
    int marker = openat(directory_fd, marker_name,
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (marker < 0) return false;
    size_t written = 0;
    while (written < sizeof(marker_contents) - 1) {
        ssize_t count = write(marker, marker_contents + written,
                              sizeof(marker_contents) - 1 - written);
        if (count > 0) written += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else break;
    }
    bool durable = written == sizeof(marker_contents) - 1 && fsync(marker) == 0;
    if (close(marker) != 0) durable = false;
    if (fsync(directory_fd) != 0) durable = false;
    // On any error the marker is retained, possibly malformed, so reopening
    // the journal fails closed instead of silently allowing another write.
    return durable;
}

bool control_intent_clear_verified(int directory_fd, struct ControlLease *lease) {
    if (control_intent_read(directory_fd) != CONTROL_INTENT_PENDING) return false;
    if (!control_lease_take_recovery_proof(lease)) return false;
    if (unlinkat(directory_fd, marker_name, 0) != 0) return false;
    return fsync(directory_fd) == 0;
}
