#include "BaselineWatch.h"

#include <assert.h>
#include <stdio.h>

static SmcBaselineSnapshot baseline(void) {
    SmcBaselineSnapshot snapshot = {0};
    snapshot.mode[0] = 3;
    snapshot.mode[1] = 3;
    return snapshot;
}

int main(void) {
    BaselineWatch watch = {0};
    SmcBaselineSnapshot normal = baseline();
    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal);
    assert(watch.status == BASELINE_WATCH_RUNNING && watch.samples == 1);
    baseline_watch_next(&watch, 1, SMC_BASELINE_OK, &normal);
    SmcBaselineSnapshot changed = normal;
    changed.ftst = 1;
    changed.mode[0] = 0;
    changed.target_rpm[0] = 1350;
    baseline_watch_next(&watch, 2, SMC_BASELINE_OK, &changed);
    assert(watch.status == BASELINE_WATCH_CHANGED && watch.samples == 3);
    baseline_watch_next(&watch, 3, SMC_BASELINE_OK, &normal);
    assert(watch.status == BASELINE_WATCH_CHANGED && watch.samples == 3);

    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal);
    for (unsigned second = 1; second <= BASELINE_WATCH_LAST_SECOND; ++second)
        baseline_watch_next(&watch, second, SMC_BASELINE_OK, &normal);
    assert(watch.status == BASELINE_WATCH_STABLE && watch.samples == 61 &&
           watch.last_second == 60);
    baseline_watch_begin(&watch, SMC_BASELINE_OPEN_FAILED, NULL);
    assert(watch.status == BASELINE_WATCH_READ_FAILED && watch.samples == 0);
    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal);
    baseline_watch_next(&watch, 1, SMC_BASELINE_READ_FAILED, NULL);
    assert(watch.status == BASELINE_WATCH_READ_FAILED && watch.samples == 1);
    puts("baseline watch tests passed");
    return 0;
}
