#ifndef VENTILATOR_SUPERVISOR_RUNNER_LIFETIME_H
#define VENTILATOR_SUPERVISOR_RUNNER_LIFETIME_H

#include <stdbool.h>

// The daemon queues a single grant on a private stdin pipe before exec.
// No thread is created in the single-threaded runner that will fork workers.
bool supervisor_runner_lifetime_enter(int descriptor);
// Parent checkpoint only: EOF, unexpected data or errors exit THIS process.
// Never signals a saved PID and never clears the recovery journal.
void supervisor_runner_lifetime_check(int descriptor);

#endif
