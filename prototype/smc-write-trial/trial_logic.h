#ifndef VENTILATOR_TRIAL_LOGIC_H
#define VENTILATOR_TRIAL_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TRIAL_FAN_COUNT = 2,
    TRIAL_TEMPERATURE_COUNT = 3,
    TRIAL_PREFLIGHT_SAMPLES = 5,
};

#define TRIAL_EXPECTED_MODEL "Mac15,7"
#define TRIAL_EXPECTED_OS_VERSION "27.0"
#define TRIAL_TARGET_DELTA_RPM 300.0
#define TRIAL_TEMPERATURE_LIMIT_C 75.0
#define TRIAL_TEMPERATURE_MAX_RISE_C 5.0

typedef struct {
    double actual_rpm;
    double target_rpm;
    double min_rpm;
    double max_rpm;
    uint8_t mode;
    char mode_type[5];
    uint32_t mode_size;
    char target_type[5];
    uint32_t target_size;
} TrialFanState;

typedef struct {
    const char *model;
    const char *os_version;
    unsigned fan_count;
    uint8_t ftst;
    char ftst_type[5];
    uint32_t ftst_size;
    TrialFanState fans[TRIAL_FAN_COUNT];
    double temperatures_c[TRIAL_PREFLIGHT_SAMPLES][TRIAL_TEMPERATURE_COUNT];
} TrialPreflight;

typedef struct {
    double target_rpm[TRIAL_FAN_COUNT];
} TrialPlan;

bool trial_make_plan(
    const TrialPreflight *input,
    TrialPlan *plan,
    char *error,
    size_t error_size);

bool trial_encode_rpm(
    const char type[5],
    uint32_t size,
    double rpm,
    uint8_t output[4],
    size_t *output_size);

bool trial_actual_reached_target(double baseline_rpm, double actual_rpm, double target_rpm);

bool trial_target_write_allowed(const TrialPlan *plan, unsigned fan, double rpm);

#endif
