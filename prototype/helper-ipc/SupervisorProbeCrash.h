#ifndef VENTILATOR_SUPERVISOR_PROBE_CRASH_H
#define VENTILATOR_SUPERVISOR_PROBE_CRASH_H

#include <stdbool.h>
#include <sys/types.h>

// Fixed diagnostic in the SINGLE-THREADED parent, after granting its observer.
// Requires pending intent and a live owned child. Never signals an input PID:
// success raises SIGKILL in THIS process; false leaves the caller running.
bool supervisor_probe_crash_after_observer_start(int directory_fd, pid_t observer);

#endif
