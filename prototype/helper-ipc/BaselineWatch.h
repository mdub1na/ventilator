#ifndef VENTILATOR_BASELINE_WATCH_H
#define VENTILATOR_BASELINE_WATCH_H

#include "SmcBaselineRead.h"

enum { BASELINE_WATCH_LAST_SECOND = 60 };

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
} BaselineWatch;

void baseline_watch_begin(BaselineWatch *watch, SmcBaselineResult result,
                          const SmcBaselineSnapshot *snapshot);
void baseline_watch_next(BaselineWatch *watch, unsigned second,
                         SmcBaselineResult result, const SmcBaselineSnapshot *snapshot);
const char *baseline_watch_status_name(BaselineWatchStatus status);

#endif
