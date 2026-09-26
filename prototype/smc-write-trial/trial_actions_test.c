#include "trial_actions.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    double now;
    bool fail_first_manual;
    bool fail_second_manual;
    bool fail_first_target;
    bool reject_zero_targets;
    bool fail_ftst_enable;
    bool apply_ftst_on_error;
    bool delay_ftst_enable;
    bool pending_ftst_enable;
    bool fail_ftst_release;
    bool fail_auto;
    bool unexpected_manual_after_unlock;
    bool stop_after_unlock;
    bool high_temperature;
    unsigned write_count;
    char writes[512];
    size_t writes_length;
    uint8_t modes[TRIAL_FAN_COUNT];
    uint8_t ftst;
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
    if (mode == 1 && fan == 0 && mock->fail_first_manual) return false;
    if (mode == 1 && fan == 1 && mock->fail_second_manual) return false;
    if (mode == 0 && mock->fail_auto) return false;
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
    if (rpm == 0 && mock->reject_zero_targets) return false;
    mock->targets[fan] = rpm;
    return true;
}

static bool mock_write_ftst(void *context, uint8_t value) {
    MockBackend *mock = context;
    char event[32];
    snprintf(event, sizeof(event), "Ftst=%u;", value);
    append_write(mock, event);
    ++mock->write_count;
    if (value == 1 && mock->fail_ftst_enable) {
        if (mock->apply_ftst_on_error) mock->ftst = 1;
        return false;
    }
    if (value == 1 && mock->delay_ftst_enable) {
        mock->pending_ftst_enable = true;
        return true;
    }
    if (value == 0 && mock->fail_ftst_release) return false;
    mock->ftst = value;
    if (value == 1 && mock->unexpected_manual_after_unlock) mock->modes[0] = 1;
    return true;
}

static bool mock_read(void *context, bool temperatures, TrialObservation *output) {
    MockBackend *mock = context;
    output->ftst = mock->ftst;
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
    if (mock->pending_ftst_enable && mock->now >= 1.0) {
        mock->pending_ftst_enable = false;
        mock->ftst = 1;
        mock->modes[0] = 0;
        mock->modes[1] = 0;
        mock->targets[0] = 1350;
        mock->targets[1] = 1458;
    }
    return true;
}

static double mock_now(void *context) {
    return ((MockBackend *)context)->now;
}

static bool mock_stop(void *context) {
    MockBackend *mock = context;
    return mock->stop_after_unlock && mock->ftst == 1;
}

static TrialBackend backend_for(MockBackend *mock) {
    return (TrialBackend){
        .context = mock,
        .write_mode = mock_write_mode,
        .write_target = mock_write_target,
        .write_ftst = mock_write_ftst,
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
           TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strstr(mock.writes, "M0=1;T0=1650;M1=1;M0=0;T0=0;M1=0;T1=0;") != NULL);
    assert(mock.modes[0] == 3 && mock.modes[1] == 3);
}

static void unsafe_temperature_enters_restore_without_waiting(void) {
    MockBackend mock = {.high_temperature = true};
    TrialBackend backend = backend_for(&mock);
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    const double baseline[TRIAL_FAN_COUNT] = {0, 0};
    assert(trial_execute_direct(&backend, &plan, baseline) ==
           TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strstr(mock.writes, "M0=0;T0=0;M1=0;T1=0;") != NULL);
}

static void direct_trial_restores_after_target_write_failure(void) {
    MockBackend mock = {.fail_first_target = true};
    TrialBackend backend = backend_for(&mock);
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    const double baseline[TRIAL_FAN_COUNT] = {0, 0};
    assert(trial_execute_direct(&backend, &plan, baseline) ==
           TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strstr(mock.writes, "M0=1;T0=1650;M0=0;T0=0;M1=0;T1=0;") != NULL);
}

static void rejected_first_mode_write_leaves_baseline_without_restore_writes(void) {
    MockBackend mock = {.fail_first_manual = true, .modes = {3, 3}};
    TrialBackend backend = backend_for(&mock);
    TrialPlan plan = {.target_rpm = {1650, 1758}};
    const double baseline[TRIAL_FAN_COUNT] = {0, 0};
    assert(trial_execute_direct(&backend, &plan, baseline) ==
           TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strcmp(mock.writes, "M0=1;") == 0);
    assert(mock.modes[0] == 3 && mock.modes[1] == 3);
}

static void system_modes_with_nonzero_target_still_require_cleanup(void) {
    MockBackend mock = {.modes = {3, 3}, .targets = {1650, 0}};
    TrialBackend backend = backend_for(&mock);
    assert(trial_restore_system(&backend));
    assert(strcmp(mock.writes, "M0=0;T0=0;M1=0;T1=0;") == 0);
    assert(mock.targets[0] == 0 && mock.targets[1] == 0);
}

static void ftst_check_round_trip_does_not_write_fan_keys(void) {
    MockBackend mock = {.modes = {3, 3}};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_SUCCEEDED);
    assert(strcmp(mock.writes, "Ftst=1;Ftst=0;") == 0);
    assert(mock.ftst == 0 && mock.modes[0] == 3 && mock.modes[1] == 3);
}

static void ftst_rejection_leaves_baseline_without_cleanup_writes(void) {
    MockBackend mock = {.modes = {3, 3}, .fail_ftst_enable = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strcmp(mock.writes, "Ftst=1;") == 0);
}

static void accepted_ftst_with_delayed_effect_never_verifies_immediate_baseline(void) {
    MockBackend mock = {.modes = {3, 3}, .delay_ftst_enable = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_WRITE_EFFECT_UNVERIFIED);
    assert(strcmp(mock.writes, "Ftst=1;") == 0);
    assert(mock.ftst == 0 && mock.pending_ftst_enable);
    assert(mock_wait(&mock, 1000));
    TrialObservation delayed = {0};
    assert(mock_read(&mock, false, &delayed));
    assert(!trial_observation_is_baseline(&delayed));
    assert(delayed.ftst == 1 && delayed.mode[0] == 0 && delayed.mode[1] == 0);
    assert(delayed.target_rpm[0] == 1350 && delayed.target_rpm[1] == 1458);
}

static void ftst_error_with_changed_readback_still_clears_unlock(void) {
    MockBackend mock = {.modes = {3, 3}, .fail_ftst_enable = true,
                        .apply_ftst_on_error = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strcmp(mock.writes, "Ftst=1;Ftst=0;") == 0);
    assert(mock.ftst == 0);
}

static void independent_restore_clears_ftst_after_unchanged_modes(void) {
    MockBackend mock = {.modes = {3, 3}, .ftst = 1};
    TrialBackend backend = backend_for(&mock);
    assert(trial_restore_unlock(&backend));
    assert(strcmp(mock.writes, "Ftst=0;") == 0);
    assert(mock.ftst == 0);
}

static void interruption_after_unlock_still_restores_ftst(void) {
    MockBackend mock = {.modes = {3, 3}, .stop_after_unlock = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strcmp(mock.writes, "Ftst=1;Ftst=0;") == 0);
    assert(mock.ftst == 0);
}

static void ftst_release_failure_requires_independent_recovery(void) {
    MockBackend mock = {.modes = {3, 3}, .fail_ftst_release = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_RESTORE_FAILED);
    assert(strcmp(mock.writes, "Ftst=1;Ftst=0;Ftst=0;Ftst=0;") == 0);
    assert(mock.ftst == 1);
}

static void ftst_is_not_cleared_while_a_fan_remains_manual(void) {
    MockBackend mock = {.modes = {3, 3}, .unexpected_manual_after_unlock = true,
                        .fail_auto = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_RESTORE_FAILED);
    assert(strstr(mock.writes, "Ftst=0;") == NULL);
    assert(mock.ftst == 1 && mock.modes[0] == 1);
}

static void ftst_restore_stops_when_minimum_targets_cannot_be_zeroed(void) {
    MockBackend mock = {.modes = {0, 0}, .ftst = 1,
                        .targets = {1350, 1458}, .reject_zero_targets = true};
    TrialBackend backend = backend_for(&mock);
    assert(!trial_restore_unlock(&backend));
    assert(strstr(mock.writes, "T0=0;") != NULL);
    assert(strstr(mock.writes, "T1=0;") != NULL);
    assert(strstr(mock.writes, "Ftst=0;") == NULL);
    assert(mock.ftst == 1);
}

static void ftst_unexpected_manual_mode_releases_fans_first(void) {
    MockBackend mock = {.modes = {3, 3}, .unexpected_manual_after_unlock = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED);
    assert(strcmp(mock.writes, "Ftst=1;M0=0;T0=0;M1=0;T1=0;Ftst=0;") == 0);
    assert(mock.ftst == 0 && mock.modes[0] == 3 && mock.modes[1] == 3);
}

static void ftst_check_rejects_nonbaseline_before_first_write(void) {
    MockBackend mock = {.modes = {3, 3}, .targets = {100, 0}};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_BASELINE_REJECTED);
    assert(mock.write_count == 0);
}

static void ftst_check_rejects_hot_reading_before_first_write(void) {
    MockBackend mock = {.modes = {3, 3}, .high_temperature = true};
    TrialBackend backend = backend_for(&mock);
    assert(trial_check_ftst(&backend) == TRIAL_RUN_BASELINE_REJECTED);
    assert(mock.write_count == 0);
}

static void baseline_observation_rejects_changed_state(void) {
    TrialObservation observation = {.mode = {3, 3}, .target_rpm = {0, 0}};
    assert(trial_observation_is_baseline(&observation));
    observation.ftst = 1;
    assert(!trial_observation_is_baseline(&observation));
    observation.ftst = 0;
    observation.mode[1] = 0;
    assert(!trial_observation_is_baseline(&observation));
    observation.mode[1] = 3;
    observation.target_rpm[0] = 1350;
    assert(!trial_observation_is_baseline(&observation));
    observation.target_rpm[0] = NAN;
    assert(!trial_observation_is_baseline(&observation));
}

typedef struct {
    unsigned reads;
    unsigned waits;
    unsigned records;
    unsigned change_at;
    unsigned fail_read_at;
    bool change_state;
    bool fail_read;
    bool fail_wait;
    bool stop_after_wait;
} FakeBaselineObserver;

static bool fake_baseline_read(void *context, TrialObservation *output) {
    FakeBaselineObserver *fake = context;
    unsigned sample = fake->reads++;
    if (fake->fail_read && sample == fake->fail_read_at) return false;
    output->mode[0] = 3;
    output->mode[1] = 3;
    if (fake->change_state && sample == fake->change_at) {
        output->ftst = 1;
        output->mode[0] = 0;
        output->mode[1] = 0;
        output->target_rpm[0] = 1350;
        output->target_rpm[1] = 1458;
    }
    return true;
}

static bool fake_baseline_wait(void *context, unsigned milliseconds) {
    FakeBaselineObserver *fake = context;
    assert(milliseconds == 1000);
    ++fake->waits;
    return !fake->fail_wait;
}

static bool fake_baseline_stopped(void *context) {
    FakeBaselineObserver *fake = context;
    return fake->stop_after_wait && fake->waits > 0;
}

static void fake_baseline_record(
    void *context, unsigned second, const TrialObservation *observation) {
    FakeBaselineObserver *fake = context;
    assert(second == fake->records);
    assert(observation != NULL);
    ++fake->records;
}

static TrialBaselineObserver baseline_backend_for(FakeBaselineObserver *fake) {
    return (TrialBaselineObserver){
        .context = fake,
        .read_observation = fake_baseline_read,
        .wait_milliseconds = fake_baseline_wait,
        .should_stop = fake_baseline_stopped,
        .record_observation = fake_baseline_record,
    };
}

static void baseline_window_requires_every_sample(void) {
    FakeBaselineObserver fake = {0};
    TrialBaselineObserver observer = baseline_backend_for(&fake);
    TrialBaselineResult result = trial_observe_baseline_window(&observer, 60);
    assert(result.status == TRIAL_BASELINE_STABLE);
    assert(result.second == 60 && result.samples == 61);
    assert(fake.reads == 61 && fake.records == 61 && fake.waits == 60);
}

static void baseline_window_catches_delayed_ftst_change(void) {
    FakeBaselineObserver fake = {.change_state = true, .change_at = 2};
    TrialBaselineObserver observer = baseline_backend_for(&fake);
    TrialBaselineResult result = trial_observe_baseline_window(&observer, 60);
    assert(result.status == TRIAL_BASELINE_CHANGED);
    assert(result.second == 2 && result.samples == 3);
    assert(fake.reads == 3 && fake.records == 3 && fake.waits == 2);
}

static void baseline_window_stops_on_read_failure(void) {
    FakeBaselineObserver fake = {.fail_read = true, .fail_read_at = 2};
    TrialBaselineObserver observer = baseline_backend_for(&fake);
    TrialBaselineResult result = trial_observe_baseline_window(&observer, 60);
    assert(result.status == TRIAL_BASELINE_READ_FAILED);
    assert(result.second == 2 && result.samples == 2);
    assert(fake.reads == 3 && fake.records == 2 && fake.waits == 2);
}

static void baseline_window_stops_on_interruption_or_wait_failure(void) {
    FakeBaselineObserver interrupted = {.stop_after_wait = true};
    TrialBaselineObserver interrupted_observer = baseline_backend_for(&interrupted);
    TrialBaselineResult result = trial_observe_baseline_window(&interrupted_observer, 60);
    assert(result.status == TRIAL_BASELINE_INTERRUPTED);
    assert(result.second == 1 && result.samples == 1);

    FakeBaselineObserver wait_failed = {.fail_wait = true};
    TrialBaselineObserver wait_failed_observer = baseline_backend_for(&wait_failed);
    result = trial_observe_baseline_window(&wait_failed_observer, 60);
    assert(result.status == TRIAL_BASELINE_WAIT_FAILED);
    assert(result.second == 0 && result.samples == 1);
}

int main(void) {
    direct_trial_success_requires_rpm_and_system_restore();
    direct_trial_restores_both_fans_after_partial_failure();
    unsafe_temperature_enters_restore_without_waiting();
    direct_trial_restores_after_target_write_failure();
    rejected_first_mode_write_leaves_baseline_without_restore_writes();
    system_modes_with_nonzero_target_still_require_cleanup();
    ftst_check_round_trip_does_not_write_fan_keys();
    ftst_rejection_leaves_baseline_without_cleanup_writes();
    accepted_ftst_with_delayed_effect_never_verifies_immediate_baseline();
    ftst_error_with_changed_readback_still_clears_unlock();
    independent_restore_clears_ftst_after_unchanged_modes();
    interruption_after_unlock_still_restores_ftst();
    ftst_release_failure_requires_independent_recovery();
    ftst_is_not_cleared_while_a_fan_remains_manual();
    ftst_restore_stops_when_minimum_targets_cannot_be_zeroed();
    ftst_unexpected_manual_mode_releases_fans_first();
    ftst_check_rejects_nonbaseline_before_first_write();
    ftst_check_rejects_hot_reading_before_first_write();
    baseline_observation_rejects_changed_state();
    baseline_window_requires_every_sample();
    baseline_window_catches_delayed_ftst_change();
    baseline_window_stops_on_read_failure();
    baseline_window_stops_on_interruption_or_wait_failure();
    puts("trial action tests passed");
    return 0;
}
