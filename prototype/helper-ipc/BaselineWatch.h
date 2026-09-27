#ifndef VENTILATOR_BASELINE_WATCH_H
#define VENTILATOR_BASELINE_WATCH_H

#include "SmcBaselineRead.h"
#include <stdbool.h>
#include <stdint.h>

enum { BASELINE_WATCH_LAST_SECOND = 60 };
#define BASELINE_WATCH_MIN_GAP_NS UINT64_C(900000000)
#define BASELINE_WATCH_MAX_GAP_NS UINT64_C(5000000000)
#define BASELINE_WATCH_DURATION_NS UINT64_C(60000000000)

typedef enum {
    BASELINE_WATCH_IDLE,
    BASELINE_WATCH_RUNNING,
    BASELINE_WATCH_STABLE,
    BASELINE_WATCH_CHANGED,
    BASELINE_WATCH_READ_FAILED,
} BaselineWatchStatus;

typedef struct {
    BaselineWatchStatus status;
    unsigned samples;
    unsigned last_second;
    SmcBaselineResult last_read_result;
    SmcBaselineSnapshot latest;
    uint64_t started_continuous_ns;
    uint64_t last_continuous_ns;
    bool timing_failed;
} BaselineWatch;

void baseline_watch_begin(BaselineWatch *watch, SmcBaselineResult result,
                          const SmcBaselineSnapshot *snapshot,
                          uint64_t sampled_continuous_ns);
void baseline_watch_next(BaselineWatch *watch, unsigned second,
                         SmcBaselineResult result, const SmcBaselineSnapshot *snapshot,
                         uint64_t sampled_continuous_ns);
const char *baseline_watch_status_name(BaselineWatchStatus status);

#endif
