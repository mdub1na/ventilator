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
    bool metrics_available;
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
    // Optional trace for a Ftst check: any observed mode/target deviation is a failed trial criterion.
    bool *observed_fan_state_change;
} TrialBackend;

typedef enum {
    TRIAL_RUN_SUCCEEDED,
    TRIAL_RUN_BASELINE_REJECTED,
    TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED,
    TRIAL_RUN_WRITE_EFFECT_UNVERIFIED,
    TRIAL_RUN_RESTORE_FAILED,
} TrialRunStatus;

typedef struct {
    void *context;
    bool (*read_observation)(void *context, TrialObservation *output);
    bool (*wait_milliseconds)(void *context, unsigned milliseconds);
    bool (*should_stop)(void *context);
    // This clock must advance during sleep as well as normal execution.
    double (*monotonic_seconds)(void *context);
    void (*record_observation)(void *context, unsigned second,
                               const TrialObservation *observation);
} TrialBaselineObserver;

typedef enum {
    TRIAL_BASELINE_STABLE,
    TRIAL_BASELINE_CHANGED,
    TRIAL_BASELINE_READ_FAILED,
    TRIAL_BASELINE_WAIT_FAILED,
    TRIAL_BASELINE_INTERRUPTED,
    TRIAL_BASELINE_TIMING_FAILED,
    TRIAL_BASELINE_INVALID,
} TrialBaselineStatus;

typedef struct {
    TrialBaselineStatus status;
    unsigned second;
    unsigned samples;
} TrialBaselineResult;

bool trial_observation_is_baseline(const TrialObservation *observation);
TrialBaselineResult trial_observe_baseline_window(
    TrialBaselineObserver *observer, unsigned last_second);

bool trial_restore_system(TrialBackend *backend);
bool trial_restore_unlock(TrialBackend *backend);

TrialRunStatus trial_check_ftst(TrialBackend *backend);

// Test-only rehearsal for the previously observed Ftst=1, mode-0, minimum-target state.
// No CLI entry point is exposed; it never writes a positive target or a fan mode.
TrialRunStatus trial_rehearse_ftst_minimum_recovery(
    TrialBackend *backend, const double minimum_rpm[TRIAL_FAN_COUNT]);

TrialRunStatus trial_execute_direct(
    TrialBackend *backend,
    const TrialPlan *plan,
    const double baseline_rpm[TRIAL_FAN_COUNT]);

#endif
