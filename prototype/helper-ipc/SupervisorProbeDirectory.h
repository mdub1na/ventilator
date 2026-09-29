#ifndef VENTILATOR_SUPERVISOR_PROBE_DIRECTORY_H
#define VENTILATOR_SUPERVISOR_PROBE_DIRECTORY_H
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char SupervisorProbeDirectoryPath[] = "/private/var/db/com.ventilator.supervisor-read-only";
static const char SupervisorProbeExecutableName[] = "supervisor-executable-v1";
static const char SupervisorProbeStagingName[] = "supervisor-executable.next";

static inline int SupervisorProbeOpenDirectory(void) {
    if (getuid() != 0 || geteuid() != 0) return -1;
    int parent = open("/private/var/db", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat parent_info = {0};
    if (parent < 0) return -1;
    if (fstat(parent, &parent_info) != 0 || parent_info.st_uid != 0 || (parent_info.st_mode & 0022) != 0) {
        close(parent); return -1;
    }
    if (mkdirat(parent, "com.ventilator.supervisor-read-only", 0700) != 0 && errno != EEXIST) {
        close(parent); return -1;
    }
    int directory = openat(parent, "com.ventilator.supervisor-read-only", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    struct stat info = {0};
    if (directory >= 0 && fstat(directory, &info) == 0 &&
        info.st_uid == 0 && (info.st_mode & 0777) == 0700) return directory;
    if (directory >= 0) (void)close(directory);
    return -1;
}
#endif
