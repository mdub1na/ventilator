#ifndef VENTILATOR_SUPERVISOR_OWNERSHIP_H
#define VENTILATOR_SUPERVISOR_OWNERSHIP_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

// Internal, fixed-file protocol in a trusted private directory. The lock is
// inherited by workers and is released by CLOSE only, never LOCK_UN.
int supervisor_ownership_acquire(int directory_fd, bool pending);
// Only the supervisor's own fork result is recorded, before granting execution.
bool supervisor_ownership_record(int directory_fd, pid_t child);
// Blocks until a durable execution grant. Then a separate child thread exits
// the whole process if the parent connection closes. Never signals a PID.
void supervisor_ownership_enter_child(int connection, uint64_t deadline_ns);

#endif
