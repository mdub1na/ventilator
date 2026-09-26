#import "BaselineWatchController.h"
#import "BaselineWatch.h"
#import "HelperStatus.h"
#import "HelperTemperatureValue.h"

#include <time.h>
#include <unistd.h>

static uint64_t monotonicNanoseconds(void) {
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * 1000000000u + (uint64_t)value.tv_nsec;
}

@interface BaselineWatchController () {
    BaselineWatch _watch;
    uint64_t _generation;
    uint64_t _startedAt;
    uint64_t _lastSampleAt;
}
@end

@implementation BaselineWatchController

+ (instancetype)shared {
    static BaselineWatchController *controller;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ controller = [BaselineWatchController new]; });
    return controller;
}

- (NSDictionary<NSString *, id> *)statusLocked {
    NSMutableDictionary<NSString *, id> *response = [@{
        @"protocol_version": @(HelperStatusProtocolVersion),
        @"state": [NSString stringWithUTF8String:baseline_watch_status_name(_watch.status)],
        @"samples": @(_watch.samples),
        @"last_second": @(_watch.last_second),
        @"daemon_pid": @(getpid()),
        @"started_monotonic_ns": @(_startedAt),
        @"last_sample_monotonic_ns": @(_lastSampleAt)
    } mutableCopy];
    if (_watch.status == BASELINE_WATCH_READ_FAILED) {
        response[@"reason"] = [NSString stringWithUTF8String:
            smc_baseline_result_name(_watch.last_read_result)];
    }
    if (_watch.samples > 0 && _watch.status != BASELINE_WATCH_READ_FAILED) {
        SmcBaselineSnapshot *snapshot = &_watch.latest;
        response[@"baseline"] = @(smc_baseline_is_system(snapshot));
        response[@"Ftst"] = @(snapshot->ftst);
        response[@"mode"] = @[@(snapshot->mode[0]), @(snapshot->mode[1])];
        response[@"target_rpm"] = @[@(snapshot->target_rpm[0]), @(snapshot->target_rpm[1])];
        response[@"actual_rpm"] = @[@(snapshot->actual_rpm[0]), @(snapshot->actual_rpm[1])];
        response[@"temperatures_c"] = @[HelperTemperatureValue(snapshot->temperatures_c[0]),
                                         HelperTemperatureValue(snapshot->temperatures_c[1]),
                                         HelperTemperatureValue(snapshot->temperatures_c[2])];
    }
    return response;
}

- (NSDictionary<NSString *, id> *)status {
    @synchronized (self) { return [self statusLocked]; }
}

- (void)scheduleSecond:(unsigned)second generation:(uint64_t)generation {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC),
                   dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        SmcBaselineSnapshot snapshot = {0};
        SmcBaselineResult result = smc_baseline_read(&snapshot);
        uint64_t sampledAt = monotonicNanoseconds();
        BOOL continueWatching = NO;
        @synchronized (self) {
            if (_generation != generation || _watch.status != BASELINE_WATCH_RUNNING) return;
            if (sampledAt == 0) result = SMC_BASELINE_UNEXPECTED_FORMAT;
            baseline_watch_next(&_watch, second, result, &snapshot);
            _lastSampleAt = sampledAt;
            continueWatching = _watch.status == BASELINE_WATCH_RUNNING;
        }
        if (continueWatching) [self scheduleSecond:second + 1 generation:generation];
    });
}

- (NSDictionary<NSString *, id> *)start {
    @synchronized (self) {
        // Preserve a detected change or read failure until the daemon exits.
        if (_watch.status == BASELINE_WATCH_RUNNING ||
            _watch.status == BASELINE_WATCH_CHANGED ||
            _watch.status == BASELINE_WATCH_READ_FAILED) return [self statusLocked];
        SmcBaselineSnapshot snapshot = {0};
        SmcBaselineResult result = smc_baseline_read(&snapshot);
        uint64_t sampledAt = monotonicNanoseconds();
        if (sampledAt == 0) result = SMC_BASELINE_UNEXPECTED_FORMAT;
        baseline_watch_begin(&_watch, result, &snapshot);
        ++_generation;
        _startedAt = sampledAt;
        _lastSampleAt = sampledAt;
        if (_watch.status == BASELINE_WATCH_RUNNING)
            [self scheduleSecond:1 generation:_generation];
        return [self statusLocked];
    }
}

@end
