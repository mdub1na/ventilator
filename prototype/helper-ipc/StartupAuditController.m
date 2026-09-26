#import "StartupAuditController.h"
#import "HelperStatus.h"

#include <time.h>
#include <unistd.h>

static uint64_t monotonicNanoseconds(void) {
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * 1000000000u + (uint64_t)value.tv_nsec;
}

@interface StartupAuditController ()
@property(nonatomic, copy) NSDictionary<NSString *, id> *capturedStatus;
@end

@implementation StartupAuditController

+ (instancetype)shared {
    static StartupAuditController *controller;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ controller = [[StartupAuditController alloc] initWithReader:smc_baseline_read]; });
    return controller;
}

- (instancetype)initWithReader:(StartupAuditReader)reader {
    self = [super init];
    if (!self) return nil;

    SmcBaselineSnapshot snapshot = {0};
    SmcBaselineResult result = reader ? reader(&snapshot) : SMC_BASELINE_UNEXPECTED_FORMAT;
    uint64_t sampledAt = monotonicNanoseconds();
    if (sampledAt == 0) result = SMC_BASELINE_UNEXPECTED_FORMAT;
    NSMutableDictionary<NSString *, id> *response = [@{
        @"protocol_version": @(HelperStatusProtocolVersion),
        @"daemon_pid": @(getpid()),
        @"control_allowed": @NO
    } mutableCopy];
    if (result != SMC_BASELINE_OK) {
        response[@"state"] = @"read_failed";
        response[@"available"] = @NO;
        response[@"reason"] = [NSString stringWithUTF8String:smc_baseline_result_name(result)];
    } else {
        BOOL baseline = smc_baseline_is_system(&snapshot);
        response[@"state"] = baseline ? @"system_at_start" : @"changed_at_start";
        response[@"available"] = @YES;
        response[@"model"] = @"Mac15,7";
        response[@"macos"] = @"27.0";
        response[@"sample_monotonic_ns"] = @(sampledAt);
        response[@"baseline"] = @(baseline);
        response[@"Ftst"] = @(snapshot.ftst);
        response[@"mode"] = @[@(snapshot.mode[0]), @(snapshot.mode[1])];
        response[@"target_rpm"] = @[@(snapshot.target_rpm[0]), @(snapshot.target_rpm[1])];
        response[@"actual_rpm"] = @[@(snapshot.actual_rpm[0]), @(snapshot.actual_rpm[1])];
        response[@"temperatures_c"] = @[@(snapshot.temperatures_c[0]),
                                         @(snapshot.temperatures_c[1]),
                                         @(snapshot.temperatures_c[2])];
    }
    self.capturedStatus = response;
    return self;
}

- (NSDictionary<NSString *, id> *)status {
    return self.capturedStatus;
}

@end
