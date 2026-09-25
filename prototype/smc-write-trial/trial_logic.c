#include "trial_logic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool fail(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
    return false;
}

static bool exact_type(const char actual[5], const char *expected) {
    return actual != NULL && strncmp(actual, expected, 4) == 0;
}

static bool supported_target_type(const TrialFanState *fan) {
    return (exact_type(fan->target_type, "flt ") && fan->target_size == 4) ||
           (exact_type(fan->target_type, "fpe2") && fan->target_size == 2);
}

bool trial_make_plan(
    const TrialPreflight *input,
    TrialPlan *plan,
    char *error,
    size_t error_size) {
    if (input == NULL || plan == NULL) return fail(error, error_size, "missing preflight data");
    if (input->model == NULL || strcmp(input->model, TRIAL_EXPECTED_MODEL) != 0) {
        return fail(error, error_size, "model is not exactly Mac15,7");
    }
    if (input->os_version == NULL || strcmp(input->os_version, TRIAL_EXPECTED_OS_VERSION) != 0) {
        return fail(error, error_size, "macOS version is not exactly 27.0");
    }
    if (input->fan_count != TRIAL_FAN_COUNT) {
        return fail(error, error_size, "fan count is not exactly two");
    }
    if (!exact_type(input->ftst_type, "ui8 ") || input->ftst_size != 1 || input->ftst != 0) {
        return fail(error, error_size, "Ftst is not an unlocked ui8 value of zero");
    }

    for (unsigned sample = 0; sample < TRIAL_PREFLIGHT_SAMPLES; ++sample) {
        for (unsigned sensor = 0; sensor < TRIAL_TEMPERATURE_COUNT; ++sensor) {
            double value = input->temperatures_c[sample][sensor];
            if (!isfinite(value) || value < 10.0 || value >= TRIAL_TEMPERATURE_LIMIT_C) {
                return fail(error, error_size, "a required temperature is unavailable or too high");
            }
        }
    }
    for (unsigned sensor = 0; sensor < TRIAL_TEMPERATURE_COUNT; ++sensor) {
        double rise = input->temperatures_c[TRIAL_PREFLIGHT_SAMPLES - 1][sensor] -
                      input->temperatures_c[0][sensor];
        if (rise > TRIAL_TEMPERATURE_MAX_RISE_C) {
            return fail(error, error_size, "a required temperature trend rose by more than 5 C");
        }
    }

    for (unsigned index = 0; index < TRIAL_FAN_COUNT; ++index) {
        const TrialFanState *fan = &input->fans[index];
        if (fan->mode != 3 || !exact_type(fan->mode_type, "ui8 ") || fan->mode_size != 1) {
            return fail(error, error_size, "a fan mode is not the expected ui8 system value 3");
        }
        if (!isfinite(fan->actual_rpm) || !isfinite(fan->target_rpm) ||
            !isfinite(fan->min_rpm) || !isfinite(fan->max_rpm) ||
            fan->actual_rpm < 0.0 || fabs(fan->target_rpm) > 1.0 ||
            fan->min_rpm < 0.0 || fan->min_rpm >= fan->max_rpm) {
            return fail(error, error_size, "a fan RPM value or range is invalid");
        }
        if (!supported_target_type(fan)) {
            return fail(error, error_size, "a target key has an unsupported type or size");
        }
        double base = fmax(fan->actual_rpm, fan->min_rpm);
        double target = base + TRIAL_TARGET_DELTA_RPM;
        if (!isfinite(target) || target > fan->max_rpm) {
            return fail(error, error_size, "a fan has less than 300 RPM of safe headroom");
        }
        plan->target_rpm[index] = target;
    }

    if (error != NULL && error_size > 0) error[0] = '\0';
    return true;
}

bool trial_encode_rpm(
    const char type[5],
    uint32_t size,
    double rpm,
    uint8_t output[4],
    size_t *output_size) {
    if (type == NULL || output == NULL || output_size == NULL || !isfinite(rpm) || rpm < 0.0) {
        return false;
    }
    if (exact_type(type, "flt ") && size == 4) {
        float value = (float)rpm;
        uint32_t bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        output[0] = (uint8_t)(bits & 0xffu);
        output[1] = (uint8_t)((bits >> 8) & 0xffu);
        output[2] = (uint8_t)((bits >> 16) & 0xffu);
        output[3] = (uint8_t)((bits >> 24) & 0xffu);
        *output_size = 4;
        return true;
    }
    if (exact_type(type, "fpe2") && size == 2 && rpm <= 16383.0) {
        uint16_t encoded = (uint16_t)(rpm * 4.0);
        output[0] = (uint8_t)(encoded >> 8);
        output[1] = (uint8_t)(encoded & 0xffu);
        output[2] = 0;
        output[3] = 0;
        *output_size = 2;
        return true;
    }
    return false;
}

bool trial_actual_reached_target(double baseline_rpm, double actual_rpm, double target_rpm) {
    if (!isfinite(baseline_rpm) || !isfinite(actual_rpm) || !isfinite(target_rpm)) return false;
    if (actual_rpm <= baseline_rpm) return false;
    double tolerance = fmax(250.0, target_rpm * 0.15);
    return fabs(actual_rpm - target_rpm) <= tolerance;
}

bool trial_target_write_allowed(const TrialPlan *plan, unsigned fan, double rpm) {
    if (!isfinite(rpm) || rpm < 0.0 || fan >= TRIAL_FAN_COUNT) return false;
    if (rpm == 0.0) return true;
    return plan != NULL && isfinite(plan->target_rpm[fan]) &&
           fabs(rpm - plan->target_rpm[fan]) <= 0.01;
}
