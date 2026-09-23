#include "trial_actions.h"

#include <math.h>
#include <stddef.h>

enum {
    RESTORE_RETRIES = 10,
    RESTORE_RETRY_DELAY_MS = 50,
    RESTORE_VERIFY_SECONDS = 5,
    OBSERVE_SECONDS = 5,
};

static bool retry_mode(TrialBackend *backend, unsigned fan, uint8_t mode) {
    for (unsigned attempt = 0; attempt < RESTORE_RETRIES; ++attempt) {
        if (backend->write_mode(backend->context, fan, mode)) return true;
        if (attempt + 1 < RESTORE_RETRIES) {
            backend->wait_milliseconds(backend->context, RESTORE_RETRY_DELAY_MS);
        }
    }
    return false;
}

static bool retry_target(TrialBackend *backend, unsigned fan, double rpm) {
    for (unsigned attempt = 0; attempt < RESTORE_RETRIES; ++attempt) {
        if (backend->write_target(backend->context, fan, rpm)) return true;
        if (attempt + 1 < RESTORE_RETRIES) {
            backend->wait_milliseconds(backend->context, RESTORE_RETRY_DELAY_MS);
        }
    }
    return false;
}

static bool restored(const TrialObservation *observation) {
    if (observation->ftst != 0) return false;
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if (observation->mode[fan] != 3) return false;
    }
    return true;
}

bool trial_restore_system(TrialBackend *backend) {
    if (backend == NULL || backend->write_mode == NULL || backend->write_target == NULL ||
        backend->read_observation == NULL || backend->wait_milliseconds == NULL) return false;

    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        (void)retry_mode(backend, fan, 0);
        (void)retry_target(backend, fan, 0.0);
    }

    for (unsigned second = 0; second <= RESTORE_VERIFY_SECONDS; ++second) {
        TrialObservation observation = {0};
        if (backend->read_observation(backend->context, false, &observation) &&
            restored(&observation)) return true;
        if (second < RESTORE_VERIFY_SECONDS) {
            backend->wait_milliseconds(backend->context, 1000);
        }
    }
    return false;
}

static bool manual_state_matches(
    const TrialObservation *observation,
    const TrialPlan *plan,
    bool require_temperatures) {
    if (observation->ftst != 0) return false;
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if (observation->mode[fan] != 1 ||
            !isfinite(observation->target_rpm[fan]) ||
            fabs(observation->target_rpm[fan] - plan->target_rpm[fan]) > 1.0) return false;
    }
    if (require_temperatures) {
        for (unsigned sensor = 0; sensor < TRIAL_TEMPERATURE_COUNT; ++sensor) {
            double value = observation->temperatures_c[sensor];
            if (!isfinite(value) || value < 10.0 || value >= TRIAL_TEMPERATURE_LIMIT_C) return false;
        }
    }
    return true;
}

TrialRunStatus trial_execute_direct(
    TrialBackend *backend,
    const TrialPlan *plan,
    const double baseline_rpm[TRIAL_FAN_COUNT]) {
    if (backend == NULL || plan == NULL || baseline_rpm == NULL ||
        backend->write_mode == NULL || backend->write_target == NULL ||
        backend->read_observation == NULL || backend->wait_milliseconds == NULL ||
        backend->monotonic_seconds == NULL || backend->should_stop == NULL) {
        return TRIAL_RUN_RESTORE_FAILED;
    }

    bool control_ok = true;
    double manual_started = backend->monotonic_seconds(backend->context);
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if (backend->should_stop(backend->context) ||
            !backend->write_mode(backend->context, fan, 1)) {
            control_ok = false;
            break;
        }
        double target_window_started = backend->monotonic_seconds(backend->context);
        if (!backend->write_target(backend->context, fan, plan->target_rpm[fan]) ||
            backend->monotonic_seconds(backend->context) - target_window_started > 0.250) {
            control_ok = false;
            break;
        }
    }

    TrialObservation observation = {0};
    if (control_ok &&
        (!backend->read_observation(backend->context, true, &observation) ||
         !manual_state_matches(&observation, plan, true) ||
         backend->monotonic_seconds(backend->context) - manual_started >= 15.0)) {
        control_ok = false;
    }

    for (unsigned second = 1; control_ok && second <= OBSERVE_SECONDS; ++second) {
        if (backend->should_stop(backend->context) ||
            backend->monotonic_seconds(backend->context) - manual_started >= 15.0 ||
            !backend->wait_milliseconds(backend->context, 1000) ||
            backend->should_stop(backend->context) ||
            !backend->read_observation(backend->context, true, &observation) ||
            !manual_state_matches(&observation, plan, true) ||
            backend->monotonic_seconds(backend->context) - manual_started >= 15.0) {
            control_ok = false;
            break;
        }
        if (backend->record_observation != NULL) {
            backend->record_observation(backend->context, second, &observation);
        }
        if (second == OBSERVE_SECONDS) {
            for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
                if (!trial_actual_reached_target(
                        baseline_rpm[fan], observation.actual_rpm[fan], plan->target_rpm[fan])) {
                    control_ok = false;
                }
            }
        }
    }

    if (!trial_restore_system(backend)) return TRIAL_RUN_RESTORE_FAILED;
    return control_ok ? TRIAL_RUN_SUCCEEDED : TRIAL_RUN_CONTROL_FAILED_RESTORED;
}
