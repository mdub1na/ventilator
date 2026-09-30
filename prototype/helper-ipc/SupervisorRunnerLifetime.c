#include "SupervisorRunnerLifetime.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void supervisor_runner_lifetime_check(int descriptor) {
    char unexpected;
    ssize_t count;
    do { count = read(descriptor, &unexpected, 1); } while (count < 0 && errno == EINTR);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    _exit(125);
}

bool supervisor_runner_lifetime_enter(int descriptor) {
    struct stat info = {0};
    int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || fstat(descriptor, &info) != 0 || !S_ISFIFO(info.st_mode) ||
        fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0) return false;
    char grant = 0;
    ssize_t count;
    do { count = read(descriptor, &grant, 1); } while (count < 0 && errno == EINTR);
    if (count != 1 || grant != 'G') return false;
    supervisor_runner_lifetime_check(descriptor);
    return true;
}
