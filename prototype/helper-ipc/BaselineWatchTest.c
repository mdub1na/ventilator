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
    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal, UINT64_C(1000000000));
    assert(watch.status == BASELINE_WATCH_RUNNING && watch.samples == 1);
    baseline_watch_next(&watch, 1, SMC_BASELINE_OK, &normal, UINT64_C(2000000000));
    SmcBaselineSnapshot changed = normal;
    changed.ftst = 1;
    changed.mode[0] = 0;
    changed.target_rpm[0] = 1350;
    baseline_watch_next(&watch, 2, SMC_BASELINE_OK, &changed, UINT64_C(3000000000));
    assert(watch.status == BASELINE_WATCH_CHANGED && watch.samples == 3);
    baseline_watch_next(&watch, 3, SMC_BASELINE_OK, &normal, UINT64_C(4000000000));
    assert(watch.status == BASELINE_WATCH_CHANGED && watch.samples == 3);

    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal, UINT64_C(1000000000));
    for (unsigned second = 1; second <= BASELINE_WATCH_LAST_SECOND; ++second)
        baseline_watch_next(&watch, second, SMC_BASELINE_OK, &normal,
                            UINT64_C(1000000000) + (uint64_t)second * UINT64_C(1000000000));
    assert(watch.status == BASELINE_WATCH_STABLE && watch.samples == 61 &&
           watch.last_second == 60);
    baseline_watch_begin(&watch, SMC_BASELINE_OPEN_FAILED, NULL, UINT64_C(1000000000));
    assert(watch.status == BASELINE_WATCH_READ_FAILED && watch.samples == 0);
    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal, UINT64_C(1000000000));
    baseline_watch_next(&watch, 1, SMC_BASELINE_READ_FAILED, NULL, UINT64_C(2000000000));
    assert(watch.status == BASELINE_WATCH_READ_FAILED && watch.samples == 1 &&
           !watch.timing_failed);

    // A delayed callback after sleep cannot turn this old window into stable.
    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal, UINT64_C(1000000000));
    baseline_watch_next(&watch, 1, SMC_BASELINE_OK, &normal, UINT64_C(22000000000));
    assert(watch.status == BASELINE_WATCH_READ_FAILED && watch.timing_failed &&
           watch.samples == 1);
    baseline_watch_next(&watch, 2, SMC_BASELINE_OK, &normal, UINT64_C(23000000000));
    assert(watch.status == BASELINE_WATCH_READ_FAILED && watch.samples == 1);

    baseline_watch_begin(&watch, SMC_BASELINE_OK, &normal, UINT64_C(1000000000));
    for (unsigned second = 1; second <= BASELINE_WATCH_LAST_SECOND; ++second)
        baseline_watch_next(&watch, second, SMC_BASELINE_OK, &normal,
                            UINT64_C(1000000000) + (uint64_t)second * UINT64_C(950000000));
    assert(watch.status == BASELINE_WATCH_READ_FAILED && watch.timing_failed &&
           watch.samples == 60 && watch.last_second == 59);
    puts("baseline watch tests passed");
    return 0;
}
