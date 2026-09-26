#include "BaselineWatch.h"

#include <string.h>

void baseline_watch_begin(BaselineWatch *watch, SmcBaselineResult result,
                          const SmcBaselineSnapshot *snapshot) {
    if (watch == NULL) return;
    memset(watch, 0, sizeof(*watch));
    watch->last_read_result = result;
    if (result != SMC_BASELINE_OK || snapshot == NULL) {
        watch->status = BASELINE_WATCH_READ_FAILED;
        return;
    }
    watch->samples = 1;
    watch->latest = *snapshot;
    watch->status = smc_baseline_is_system(snapshot) ?
                    BASELINE_WATCH_RUNNING : BASELINE_WATCH_CHANGED;
}

void baseline_watch_next(BaselineWatch *watch, unsigned second,
                         SmcBaselineResult result, const SmcBaselineSnapshot *snapshot) {
    if (watch == NULL || watch->status != BASELINE_WATCH_RUNNING) return;
    if (second != watch->last_second + 1 || second > BASELINE_WATCH_LAST_SECOND ||
        result != SMC_BASELINE_OK || snapshot == NULL) {
        watch->status = BASELINE_WATCH_READ_FAILED;
        watch->last_read_result = result == SMC_BASELINE_OK ?
                                  SMC_BASELINE_UNEXPECTED_FORMAT : result;
        return;
    }
    watch->latest = *snapshot;
    watch->samples += 1;
    watch->last_second = second;
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
