#include "trial_actions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    double now;
    bool fail_second_manual;
    bool fail_first_target;
    bool high_temperature;
    unsigned write_count;
    char writes[512];
    size_t writes_length;
    uint8_t modes[TRIAL_FAN_COUNT];
    double targets[TRIAL_FAN_COUNT];
    bool restoring;
} MockBackend;

static void append_write(MockBackend *mock, const char *text) {
    size_t length = strlen(text);
    assert(mock->writes_length + length + 1 < sizeof(mock->writes));
    memcpy(mock->writes + mock->writes_length, text, length + 1);
    mock->writes_length += length;
}

static bool mock_write_mode(void *context, unsigned fan, uint8_t mode) {
    MockBackend *mock = context;
    char event[32];
    snprintf(event, sizeof(event), "M%u=%u;", fan, mode);
    append_write(mock, event);
    ++mock->write_count;
    if (mode == 1 && fan == 1 && mock->fail_second_manual) return false;
    mock->modes[fan] = mode;
    if (mode == 0) mock->restoring = true;
    return true;
}

static bool mock_write_target(void *context, unsigned fan, double rpm) {
    MockBackend *mock = context;
    char event[48];
    snprintf(event, sizeof(event), "T%u=%.0f;", fan, rpm);
    append_write(mock, event);
    ++mock->write_count;
    if (rpm > 0 && fan == 0 && mock->fail_first_target) return false;
    mock->targets[fan] = rpm;
    return true;
}

static bool mock_read(void *context, bool temperatures, TrialObservation *output) {
    MockBackend *mock = context;
    output->ftst = 0;
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        if (mock->restoring) mock->modes[fan] = 3;
        output->mode[fan] = mock->modes[fan];
        output->target_rpm[fan] = mock->targets[fan];
        output->actual_rpm[fan] = mock->now >= 5.0 ? mock->targets[fan] : mock->targets[fan] * 0.9;
    }
    if (temperatures) {
        output->temperatures_c[0] = mock->high_temperature ? 75.0 : 55.0;
        output->temperatures_c[1] = 50.0;
        output->temperatures_c[2] = 35.0;
    }
    return true;
}

static bool mock_wait(void *context, unsigned milliseconds) {
    MockBackend *mock = context;
    mock->now += milliseconds / 1000.0;
    return true;
}

static double mock_now(void *context) {
    return ((MockBackend *)context)->now;
}

static bool mock_stop(void *context) {
    (void)context;
    return false;
}

static TrialBackend backend_for(MockBackend *mock) {
    return (TrialBackend){
        .context = mock,
        .write_mode = mock_write_mode,
        .write_target = mock_write_target,
        .read_observation = mock_read,
        .wait_milliseconds = mock_wait,
        .monotonic_seconds = mock_now,
        .should_stop = mock_stop,
    };
}

static void direct_trial_success_requires_rpm_and_system_restore(void) {
    MockBackend mock = {0};
    TrialBackend backend = backend_for(&mock);
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    const double baseline[TRIAL_FAN_COUNT] = {0, 0};
    assert(trial_execute_direct(&backend, &plan, baseline) == TRIAL_RUN_SUCCEEDED);
    assert(strcmp(
        mock.writes,
        "M0=1;T0=1650;M1=1;T1=1758;M0=0;T0=0;M1=0;T1=0;") == 0);
    assert(mock.modes[0] == 3 && mock.modes[1] == 3);
}

static void direct_trial_restores_both_fans_after_partial_failure(void) {
    MockBackend mock = {.fail_second_manual = true};
    TrialBackend backend = backend_for(&mock);
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    const double baseline[TRIAL_FAN_COUNT] = {0, 0};
    assert(trial_execute_direct(&backend, &plan, baseline) ==
           TRIAL_RUN_CONTROL_FAILED_RESTORED);
    assert(strstr(mock.writes, "M0=1;T0=1650;M1=1;M0=0;T0=0;M1=0;T1=0;") != NULL);
    assert(mock.modes[0] == 3 && mock.modes[1] == 3);
}

static void unsafe_temperature_enters_restore_without_waiting(void) {
    MockBackend mock = {.high_temperature = true};
    TrialBackend backend = backend_for(&mock);
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    const double baseline[TRIAL_FAN_COUNT] = {0, 0};
    assert(trial_execute_direct(&backend, &plan, baseline) ==
           TRIAL_RUN_CONTROL_FAILED_RESTORED);
    assert(strstr(mock.writes, "M0=0;T0=0;M1=0;T1=0;") != NULL);
}

static void direct_trial_restores_after_target_write_failure(void) {
    MockBackend mock = {.fail_first_target = true};
    TrialBackend backend = backend_for(&mock);
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    const double baseline[TRIAL_FAN_COUNT] = {0, 0};
    assert(trial_execute_direct(&backend, &plan, baseline) ==
           TRIAL_RUN_CONTROL_FAILED_RESTORED);
    assert(strstr(mock.writes, "M0=1;T0=1650;M0=0;T0=0;M1=0;T1=0;") != NULL);
}

int main(void) {
    direct_trial_success_requires_rpm_and_system_restore();
    direct_trial_restores_both_fans_after_partial_failure();
    unsafe_temperature_enters_restore_without_waiting();
    direct_trial_restores_after_target_write_failure();
    puts("trial action tests passed");
    return 0;
}
