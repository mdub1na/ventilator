#include "trial_actions.h"

#include <math.h>
#include <stddef.h>

enum {
    RESTORE_RETRIES = 10,
    RESTORE_RETRY_DELAY_MS = 50,
    RESTORE_VERIFY_SECONDS = 5,
    OBSERVE_SECONDS = 5,
    FTST_RELEASE_RETRIES = 3,
    FTST_RELEASE_WAIT_SECONDS = 10,
    FTST_BASELINE_WAIT_SECONDS = 90,
    FTST_BASELINE_STABLE_SECONDS = 60,
    FTST_EFFECT_WAIT_HALF_SECONDS = 20,
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

bool trial_observation_is_baseline(const TrialObservation *observation) {
    if (observation == NULL || observation->ftst != 0) return false;
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if (observation->mode[fan] != 3 ||
            !isfinite(observation->target_rpm[fan]) ||
            fabs(observation->target_rpm[fan]) > 1.0) return false;
    }
    return true;
}

TrialBaselineResult trial_observe_baseline_window(
    TrialBaselineObserver *observer, unsigned last_second) {
    TrialBaselineResult result = {.status = TRIAL_BASELINE_INVALID};
    if (observer == NULL || observer->read_observation == NULL ||
        observer->wait_milliseconds == NULL || observer->should_stop == NULL ||
        observer->record_observation == NULL) return result;

    for (unsigned second = 0; ; ++second) {
        result.second = second;
        if (observer->should_stop(observer->context)) {
            result.status = TRIAL_BASELINE_INTERRUPTED;
            return result;
        }
        TrialObservation observation = {0};
        if (!observer->read_observation(observer->context, &observation)) {
            result.status = TRIAL_BASELINE_READ_FAILED;
            return result;
        }
        ++result.samples;
        if (observer->should_stop(observer->context)) {
            result.status = TRIAL_BASELINE_INTERRUPTED;
            return result;
        }
        observer->record_observation(observer->context, second, &observation);
        if (!trial_observation_is_baseline(&observation)) {
            result.status = TRIAL_BASELINE_CHANGED;
            return result;
        }
        if (observer->should_stop(observer->context)) {
            result.status = TRIAL_BASELINE_INTERRUPTED;
            return result;
        }
        if (second == last_second) {
            result.status = TRIAL_BASELINE_STABLE;
            return result;
        }
        if (!observer->wait_milliseconds(observer->context, 1000)) {
            result.status = observer->should_stop(observer->context) ?
                            TRIAL_BASELINE_INTERRUPTED : TRIAL_BASELINE_WAIT_FAILED;
            return result;
        }
    }
}

static bool modes_released(const TrialObservation *observation) {
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if ((observation->mode[fan] != 0 && observation->mode[fan] != 3) ||
            !isfinite(observation->target_rpm[fan]) ||
            observation->target_rpm[fan] < 0.0) return false;
    }
    return true;
}

static bool system_modes_and_zero_targets(const TrialObservation *observation) {
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if (observation->mode[fan] != 3 ||
            !isfinite(observation->target_rpm[fan]) ||
            fabs(observation->target_rpm[fan]) > 1.0) return false;
    }
    return true;
}

static bool temperatures_safe(const TrialObservation *observation) {
    for (unsigned sensor = 0; sensor < TRIAL_TEMPERATURE_COUNT; ++sensor) {
        double temperature = observation->temperatures_c[sensor];
        if (!isfinite(temperature) || temperature < 10.0 ||
            temperature >= TRIAL_TEMPERATURE_LIMIT_C) return false;
    }
    return true;
}

bool trial_restore_system(TrialBackend *backend) {
    if (backend == NULL || backend->write_mode == NULL || backend->write_target == NULL ||
        backend->read_observation == NULL || backend->wait_milliseconds == NULL) return false;

    // A rejected mode write may leave the exact baseline untouched. Read it
    // before recovery so we do not issue more writes to an already safe SMC.
    TrialObservation initial = {0};
    if (backend->read_observation(backend->context, false, &initial) &&
        trial_observation_is_baseline(&initial)) {
        return true;
    }

    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        (void)retry_mode(backend, fan, 0);
        (void)retry_target(backend, fan, 0.0);
    }

    for (unsigned second = 0; second <= RESTORE_VERIFY_SECONDS; ++second) {
        TrialObservation observation = {0};
        if (backend->read_observation(backend->context, false, &observation) &&
            trial_observation_is_baseline(&observation)) return true;
        if (second < RESTORE_VERIFY_SECONDS) {
            backend->wait_milliseconds(backend->context, 1000);
        }
    }
    return false;
}

bool trial_restore_unlock(TrialBackend *backend) {
    if (backend == NULL || backend->write_ftst == NULL ||
        backend->write_mode == NULL || backend->write_target == NULL ||
        backend->read_observation == NULL || backend->wait_milliseconds == NULL) return false;

    TrialObservation current = {0};
    bool read_ok = false;
    for (unsigned attempt = 0; attempt < FTST_RELEASE_RETRIES; ++attempt) {
        if (backend->read_observation(backend->context, false, &current)) {
            read_ok = true;
            break;
        }
        if (attempt + 1 < FTST_RELEASE_RETRIES) {
            backend->wait_milliseconds(backend->context, RESTORE_RETRY_DELAY_MS);
        }
    }
    if (!read_ok) return false;
    if (backend->record_observation != NULL) {
        backend->record_observation(backend->context, 0, &current);
    }
    if (trial_observation_is_baseline(&current)) return true;
    if (current.ftst == 0) return trial_restore_system(backend);
    if (current.ftst != 1) return false;

    // Mode 0 is treated as released by this trial protocol. The observed
    // Ftst=1 state had mode 0 with minimum targets; requiring zero targets here
    // may have prevented the earlier recovery attempt from clearing Ftst.
    // Never clear Ftst while a fan still reports manual mode 1.
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if (current.mode[fan] == 1 && !retry_mode(backend, fan, 0)) return false;
    }
    if (!backend->read_observation(backend->context, false, &current) ||
        current.ftst != 1 || !modes_released(&current)) return false;
    if (backend->record_observation != NULL) {
        backend->record_observation(backend->context, 0, &current);
    }

    bool ftst_cleared = false;
    for (unsigned attempt = 0; attempt < FTST_RELEASE_RETRIES && !ftst_cleared; ++attempt) {
        (void)backend->write_ftst(backend->context, 0);
        for (unsigned second = 0; second < FTST_RELEASE_WAIT_SECONDS; ++second) {
            TrialObservation after_write = {0};
            if (!backend->read_observation(backend->context, false, &after_write) ||
                !modes_released(&after_write)) return false;
            if (backend->record_observation != NULL) {
                backend->record_observation(backend->context, second, &after_write);
            }
            if (after_write.ftst == 0) {
                ftst_cleared = true;
                break;
            }
            if (after_write.ftst != 1 ||
                !backend->wait_milliseconds(backend->context, 1000)) return false;
        }
    }
    if (!ftst_cleared) return false;

    // A single baseline read after an accepted Ftst write was misleading on
    // Mac15,7. Require an uninterrupted minute, allowing 30 s for takeover.
    unsigned stable_seconds = 0;
    for (unsigned second = 0; second <= FTST_BASELINE_WAIT_SECONDS; ++second) {
        TrialObservation observation = {0};
        if (!backend->read_observation(backend->context, false, &observation) ||
            observation.ftst != 0 || !modes_released(&observation)) return false;
        if (backend->record_observation != NULL) {
            backend->record_observation(backend->context, second, &observation);
        }
        stable_seconds = trial_observation_is_baseline(&observation) ?
                         stable_seconds + 1 : 0;
        if (stable_seconds > FTST_BASELINE_STABLE_SECONDS) return true;
        if (second < FTST_BASELINE_WAIT_SECONDS &&
            !backend->wait_milliseconds(backend->context, 1000)) return false;
    }
    return false;
}

typedef struct {
    TrialBackend *backend;
} TrialBackendObserverContext;

static bool trial_backend_observer_read(void *context, TrialObservation *output) {
    TrialBackend *backend = ((TrialBackendObserverContext *)context)->backend;
    return backend->read_observation(backend->context, false, output);
}

static bool trial_backend_observer_wait(void *context, unsigned milliseconds) {
    TrialBackend *backend = ((TrialBackendObserverContext *)context)->backend;
    return backend->wait_milliseconds(backend->context, milliseconds);
}

static bool trial_backend_observer_stopped(void *context) {
    TrialBackend *backend = ((TrialBackendObserverContext *)context)->backend;
    return backend->should_stop(backend->context);
}

static void trial_backend_observer_record(
    void *context, unsigned second, const TrialObservation *observation) {
    TrialBackend *backend = ((TrialBackendObserverContext *)context)->backend;
    if (backend->record_observation != NULL) {
        backend->record_observation(backend->context, second, observation);
    }
}

static TrialBaselineResult trial_observe_after_ftst(TrialBackend *backend) {
    TrialBackendObserverContext context = {.backend = backend};
    TrialBaselineObserver observer = {
        .context = &context,
        .read_observation = trial_backend_observer_read,
        .wait_milliseconds = trial_backend_observer_wait,
        .should_stop = trial_backend_observer_stopped,
        .record_observation = trial_backend_observer_record,
    };
    return trial_observe_baseline_window(&observer, FTST_BASELINE_STABLE_SECONDS);
}

TrialRunStatus trial_check_ftst(TrialBackend *backend) {
    if (backend == NULL || backend->write_ftst == NULL ||
        backend->write_mode == NULL || backend->write_target == NULL ||
        backend->read_observation == NULL || backend->wait_milliseconds == NULL ||
        backend->monotonic_seconds == NULL || backend->should_stop == NULL) {
        return TRIAL_RUN_RESTORE_FAILED;
    }

    TrialObservation baseline = {0};
    if (!backend->read_observation(backend->context, true, &baseline) ||
        !trial_observation_is_baseline(&baseline) || !temperatures_safe(&baseline) ||
        backend->should_stop(backend->context)) {
        return TRIAL_RUN_BASELINE_REJECTED;
    }

    double started = backend->monotonic_seconds(backend->context);
    if (!isfinite(started) || started < 0.0) return TRIAL_RUN_BASELINE_REJECTED;
    bool write_ok = backend->write_ftst(backend->context, 1);
    TrialObservation after_write = {0};
    bool observed_effect = false;
    for (unsigned half_second = 0; half_second <= FTST_EFFECT_WAIT_HALF_SECONDS;
         ++half_second) {
        if (!backend->read_observation(backend->context, true, &after_write)) {
            break;
        }
        if (backend->record_observation != NULL) {
            backend->record_observation(backend->context, half_second, &after_write);
        }
        if (!trial_observation_is_baseline(&after_write)) {
            observed_effect = true;
            break;
        }
        if (!temperatures_safe(&after_write)) break;
        if (backend->should_stop(backend->context) ||
            half_second == FTST_EFFECT_WAIT_HALF_SECONDS ||
            !backend->wait_milliseconds(backend->context, 500)) break;
    }

    if (observed_effect) {
        bool unlock_ok = write_ok && after_write.ftst == 1 &&
                         system_modes_and_zero_targets(&after_write) &&
                         temperatures_safe(&after_write) &&
                         backend->monotonic_seconds(backend->context) - started < 5.0 &&
                         !backend->should_stop(backend->context);
        if (!trial_restore_unlock(backend)) return TRIAL_RUN_RESTORE_FAILED;
        if (after_write.ftst == 0 &&
            trial_observe_after_ftst(backend).status != TRIAL_BASELINE_STABLE) {
            return TRIAL_RUN_RESTORE_FAILED;
        }
        return unlock_ok ? TRIAL_RUN_SUCCEEDED : TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED;
    }

    // An unsuccessful transport response does not prove that the command was
    // discarded by SMC. Issue the release for either result and keep watching
    // even if that release also reports failure.
    bool release_ok = backend->write_ftst(backend->context, 0);
    TrialBaselineResult later = trial_observe_after_ftst(backend);
    if (later.status != TRIAL_BASELINE_STABLE) {
        if (!trial_restore_unlock(backend) ||
            trial_observe_after_ftst(backend).status != TRIAL_BASELINE_STABLE) {
            return TRIAL_RUN_RESTORE_FAILED;
        }
    }
    if (!release_ok) return TRIAL_RUN_RESTORE_FAILED;
    // Even a stable minute cannot exclude an effect after this process exits.
    return TRIAL_RUN_WRITE_EFFECT_UNVERIFIED;
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
    bool manual_state_observed = false;
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
    if (control_ok) {
        manual_state_observed =
            backend->read_observation(backend->context, true, &observation) &&
            manual_state_matches(&observation, plan, true);
        if (!manual_state_observed ||
            backend->monotonic_seconds(backend->context) - manual_started >= 15.0) {
            control_ok = false;
        }
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
    if (trial_observe_after_ftst(backend).status != TRIAL_BASELINE_STABLE) {
        if (!trial_restore_system(backend) ||
            trial_observe_after_ftst(backend).status != TRIAL_BASELINE_STABLE) {
            return TRIAL_RUN_RESTORE_FAILED;
        }
    }
    // A rejected write can take effect after an immediate baseline read. When
    // manual control was never seen, the write effect remains unverified even
    // after a bounded observation window.
    if (!manual_state_observed) return TRIAL_RUN_WRITE_EFFECT_UNVERIFIED;
    return control_ok ? TRIAL_RUN_SUCCEEDED : TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED;
}
