#ifndef VENTILATOR_SUPERVISOR_PROBE_STORAGE_H
#define VENTILATOR_SUPERVISOR_PROBE_STORAGE_H
#include <stdbool.h>

// Internal descriptors only. Caller opens a private, owned directory without
// following links. The launcher lock is inherited by every supervised child.
int supervisor_probe_lock(int directory_fd);
bool supervisor_probe_cleanup(int directory_fd);

#endif
