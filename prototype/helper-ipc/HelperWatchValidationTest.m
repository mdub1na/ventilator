#import "HelperWatchValidation.h"

int main(void) {
    @autoreleasepool {
        NSMutableDictionary *status = [@{
            @"protocol_version": @(HelperStatusProtocolVersion),
            @"state": @"running", @"samples": @1, @"last_second": @0,
            @"daemon_pid": @123, @"started_monotonic_ns": @100,
            @"last_sample_monotonic_ns": @100, @"baseline": @YES,
            @"Ftst": @0, @"mode": @[@3, @3], @"target_rpm": @[@0, @0],
            @"actual_rpm": @[@0, @0], @"temperatures_c": @[@50, @40, @30]
        } mutableCopy];
        NSCAssert(HelperWatchResponseValid(status), @"running baseline must pass");
        status[@"state"] = @"stable";
        NSCAssert(!HelperWatchResponseValid(status), @"early stable must fail");
        status[@"samples"] = @61;
        status[@"last_second"] = @60;
        NSCAssert(HelperWatchResponseValid(status), @"full stable window must pass");
        status[@"target_rpm"] = @[@1350, @1458];
        NSCAssert(!HelperWatchResponseValid(status), @"false baseline claim must fail");
        status[@"state"] = @"changed";
        status[@"baseline"] = @NO;
        NSCAssert(HelperWatchResponseValid(status), @"detected change must pass");
        status[@"temperatures_c"] = @[@50, [NSNull null], @30];
        NSCAssert(HelperWatchResponseValid(status), @"unavailable temperature must remain distinct");
        status[@"temperatures_c"] = @[@50, @(-1.95), @30];
        NSCAssert(!HelperWatchResponseValid(status), @"implausible temperature must fail");
        puts("XPC watch validation tests passed");
        return 0;
    }
}
