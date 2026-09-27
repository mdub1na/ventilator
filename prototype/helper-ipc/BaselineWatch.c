#include "BaselineWatch.h"

#include <string.h>

void baseline_watch_begin(BaselineWatch *watch, SmcBaselineResult result,
                          const SmcBaselineSnapshot *snapshot,
                          uint64_t sampled_continuous_ns) {
    if (watch == NULL) return;
    memset(watch, 0, sizeof(*watch));
    watch->last_read_result = result;
    if (result != SMC_BASELINE_OK || snapshot == NULL) {
        watch->status = BASELINE_WATCH_READ_FAILED;
        return;
    }
    if (sampled_continuous_ns == 0) {
        watch->status = BASELINE_WATCH_READ_FAILED;
        watch->timing_failed = true;
        return;
    }
    watch->samples = 1;
    watch->latest = *snapshot;
    watch->started_continuous_ns = sampled_continuous_ns;
    watch->last_continuous_ns = sampled_continuous_ns;
    watch->status = smc_baseline_is_system(snapshot) ?
                    BASELINE_WATCH_RUNNING : BASELINE_WATCH_CHANGED;
}

void baseline_watch_next(BaselineWatch *watch, unsigned second,
                         SmcBaselineResult result, const SmcBaselineSnapshot *snapshot,
                         uint64_t sampled_continuous_ns) {
    if (watch == NULL || watch->status != BASELINE_WATCH_RUNNING) return;
    if (second != watch->last_second + 1 || second > BASELINE_WATCH_LAST_SECOND ||
        result != SMC_BASELINE_OK || snapshot == NULL) {
        watch->status = BASELINE_WATCH_READ_FAILED;
        watch->last_read_result = result == SMC_BASELINE_OK ?
                                  SMC_BASELINE_UNEXPECTED_FORMAT : result;
        return;
    }
    if (sampled_continuous_ns <= watch->last_continuous_ns ||
        sampled_continuous_ns - watch->last_continuous_ns < BASELINE_WATCH_MIN_GAP_NS ||
        sampled_continuous_ns - watch->last_continuous_ns > BASELINE_WATCH_MAX_GAP_NS ||
        (second == BASELINE_WATCH_LAST_SECOND &&
         sampled_continuous_ns - watch->started_continuous_ns < BASELINE_WATCH_DURATION_NS)) {
        watch->status = BASELINE_WATCH_READ_FAILED;
        watch->timing_failed = true;
        return;
    }
    watch->latest = *snapshot;
    watch->samples += 1;
    watch->last_second = second;
    watch->last_continuous_ns = sampled_continuous_ns;
    if (!smc_baseline_is_system(snapshot)) watch->status = BASELINE_WATCH_CHANGED;
    else if (second == BASELINE_WATCH_LAST_SECOND) watch->status = BASELINE_WATCH_STABLE;
}

const char *baseline_watch_status_name(BaselineWatchStatus status) {
    switch (status) {
        case BASELINE_WATCH_IDLE: return "idle";
        case BASELINE_WATCH_RUNNING: return "running";
        case BASELINE_WATCH_STABLE: return "stable";
        case BASELINE_WATCH_CHANGED: return "changed";
        case BASELINE_WATCH_READ_FAILED: return "read_failed";
    }
    return "unknown";
}
