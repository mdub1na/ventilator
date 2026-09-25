#ifndef VENTILATOR_TRIAL_ACTIONS_H
#define VENTILATOR_TRIAL_ACTIONS_H

#include "trial_logic.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t mode[TRIAL_FAN_COUNT];
    uint8_t ftst;
    double actual_rpm[TRIAL_FAN_COUNT];
    double target_rpm[TRIAL_FAN_COUNT];
    double temperatures_c[TRIAL_TEMPERATURE_COUNT];
} TrialObservation;

typedef struct {
    void *context;
    bool (*write_mode)(void *context, unsigned fan, uint8_t mode);
    bool (*write_target)(void *context, unsigned fan, double rpm);
    bool (*write_ftst)(void *context, uint8_t value);
    bool (*read_observation)(void *context, bool include_temperatures, TrialObservation *output);
    bool (*wait_milliseconds)(void *context, unsigned milliseconds);
    double (*monotonic_seconds)(void *context);
    bool (*should_stop)(void *context);
    void (*record_observation)(void *context, unsigned second, const TrialObservation *observation);
} TrialBackend;

typedef enum {
    TRIAL_RUN_SUCCEEDED,
    TRIAL_RUN_BASELINE_REJECTED,
    TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED,
    TRIAL_RUN_RESTORE_FAILED,
} TrialRunStatus;

bool trial_restore_system(TrialBackend *backend);
bool trial_restore_unlock(TrialBackend *backend);

TrialRunStatus trial_check_ftst(TrialBackend *backend);

TrialRunStatus trial_execute_direct(
    TrialBackend *backend,
    const TrialPlan *plan,
    const double baseline_rpm[TRIAL_FAN_COUNT]);

#endif
