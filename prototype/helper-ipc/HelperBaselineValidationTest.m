#import "HelperBaselineValidation.h"

int main(void) {
    @autoreleasepool {
        NSDictionary *valid = @{@"protocol_version": @(HelperStatusProtocolVersion),
            @"available": @YES, @"model": @"Mac15,7", @"macos": @"27.0",
            @"sample_monotonic_ns": @1, @"baseline": @YES, @"Ftst": @0,
            @"mode": @[@3, @3], @"target_rpm": @[@0, @0],
            @"actual_rpm": @[@0, @0], @"temperatures_c": @[@50, @40, @30]};
        NSCAssert(HelperBaselineResponseValid(valid), @"valid baseline must pass");
        NSMutableDictionary *changed = [valid mutableCopy];
        changed[@"target_rpm"] = @[@1350, @1458];
        NSCAssert(!HelperBaselineResponseValid(changed), @"false baseline claim must fail");
        changed[@"baseline"] = @NO;
        NSCAssert(HelperBaselineResponseValid(changed), @"changed snapshot must pass as changed");
        changed[@"temperatures_c"] = @[@50, @"invalid", @30];
        NSCAssert(!HelperBaselineResponseValid(changed), @"non-numeric temperature must fail");
        puts("XPC baseline validation tests passed");
        return 0;
    }
}
