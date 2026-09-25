#include "trial_logic.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static TrialPreflight valid_preflight(void) {
    TrialPreflight input = {0};
    input.model = TRIAL_EXPECTED_MODEL;
    input.os_version = TRIAL_EXPECTED_OS_VERSION;
    input.fan_count = TRIAL_FAN_COUNT;
    input.ftst = 0;
    memcpy(input.ftst_type, "ui8 ", 5);
    input.ftst_size = 1;
    input.fans[0] = (TrialFanState){
        .actual_rpm = 0,
        .target_rpm = 0,
        .min_rpm = 1350,
        .max_rpm = 5349,
        .mode = 3,
        .mode_type = "ui8 ",
        .mode_size = 1,
        .target_type = "flt ",
        .target_size = 4,
    };
    input.fans[1] = (TrialFanState){
        .actual_rpm = 1600,
        .target_rpm = 0,
        .min_rpm = 1458,
        .max_rpm = 5777,
        .mode = 3,
        .mode_type = "ui8 ",
        .mode_size = 1,
        .target_type = "fpe2",
        .target_size = 2,
    };
    for (unsigned sample = 0; sample < TRIAL_PREFLIGHT_SAMPLES; ++sample) {
        input.temperatures_c[sample][0] = 50.0 + sample * 0.5;
        input.temperatures_c[sample][1] = 45.0 + sample * 0.25;
        input.temperatures_c[sample][2] = 35.0;
    }
    return input;
}

static void valid_preflight_builds_targets_above_current_cooling(void) {
    TrialPreflight input = valid_preflight();
    TrialPlan plan = {0};
    char error[160];
    assert(trial_make_plan(&input, &plan, error, sizeof(error)));
    assert(plan.target_rpm[0] == 1650.0);
    assert(plan.target_rpm[1] == 1900.0);
}

static void preflight_rejects_every_allowlist_mismatch(void) {
    TrialPlan plan = {0};
    char error[160];
    TrialPreflight input = valid_preflight();
    input.model = "Mac15,8";
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    input.ftst = 1;
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    input.fans[0].mode = 1;
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    memcpy(input.fans[0].target_type, "ui16", 5);
    input.fans[0].target_size = 2;
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    input.fans[0].target_rpm = 1650;
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    input.fans[1].actual_rpm = 5600;
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    input.temperatures_c[4][0] = 75.0;
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    input.temperatures_c[4][1] = input.temperatures_c[0][1] + 5.01;
    assert(!trial_make_plan(&input, &plan, error, sizeof(error)));

    input = valid_preflight();
    input.temperatures_c[2][1] = input.temperatures_c[0][1] + 12.0;
    assert(trial_make_plan(&input, &plan, error, sizeof(error)));
}

static void rpm_encoding_follows_runtime_key_type(void) {
    uint8_t bytes[4] = {0};
    size_t size = 0;
    assert(trial_encode_rpm("flt ", 4, 1650.0, bytes, &size));
    assert(size == 4);
    uint32_t bits = (uint32_t)bytes[0] |
                    ((uint32_t)bytes[1] << 8) |
                    ((uint32_t)bytes[2] << 16) |
                    ((uint32_t)bytes[3] << 24);
    float decoded = 0;
    memcpy(&decoded, &bits, sizeof(decoded));
    assert(fabs(decoded - 1650.0f) < 0.01f);

    assert(trial_encode_rpm("fpe2", 2, 1458.0, bytes, &size));
    assert(size == 2);
    assert((((uint16_t)bytes[0] << 8) | bytes[1]) == 1458u * 4u);
    assert(!trial_encode_rpm("ui16", 2, 1458.0, bytes, &size));
}

static void observed_rpm_requires_growth_and_target_tolerance(void) {
    assert(trial_actual_reached_target(0, 1500, 1650));
    assert(trial_actual_reached_target(1600, 1850, 1900));
    assert(!trial_actual_reached_target(1600, 1600, 1900));
    assert(!trial_actual_reached_target(0, 1000, 1650));
}

static void transport_allows_only_planned_targets_or_zero(void) {
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    assert(trial_target_write_allowed(&plan, 0, 1650));
    assert(trial_target_write_allowed(&plan, 1, 1758));
    assert(trial_target_write_allowed(NULL, 0, 0));
    assert(!trial_target_write_allowed(&plan, 0, 1800));
    assert(!trial_target_write_allowed(NULL, 0, 1650));
    assert(!trial_target_write_allowed(&plan, 2, 1650));
}

int main(void) {
    valid_preflight_builds_targets_above_current_cooling();
    preflight_rejects_every_allowlist_mismatch();
    rpm_encoding_follows_runtime_key_type();
    observed_rpm_requires_growth_and_target_tolerance();
    transport_allows_only_planned_targets_or_zero();
    puts("trial logic tests passed");
    return 0;
}
