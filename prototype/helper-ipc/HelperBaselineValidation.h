#import "HelperStatus.h"
#include <math.h>

static inline BOOL HelperBaselineResponseValid(NSDictionary<NSString *, id> *result) {
    if (![result isKindOfClass:NSDictionary.class] || result.count != 11 ||
        ![result[@"protocol_version"] isEqual:@(HelperStatusProtocolVersion)] ||
        ![result[@"available"] isEqual:@YES] ||
        ![result[@"model"] isEqual:@"Mac15,7"] ||
        ![result[@"macos"] isEqual:@"27.0"] ||
        ![result[@"sample_monotonic_ns"] isKindOfClass:NSNumber.class] ||
        [result[@"sample_monotonic_ns"] unsignedLongLongValue] == 0 ||
        ![result[@"baseline"] isKindOfClass:NSNumber.class] ||
        ![result[@"Ftst"] isKindOfClass:NSNumber.class]) return NO;

    NSArray *mode = result[@"mode"];
    NSArray *target = result[@"target_rpm"];
    NSArray *actual = result[@"actual_rpm"];
    NSArray *temperatures = result[@"temperatures_c"];
    if (![mode isKindOfClass:NSArray.class] || mode.count != 2 ||
        ![target isKindOfClass:NSArray.class] || target.count != 2 ||
        ![actual isKindOfClass:NSArray.class] || actual.count != 2 ||
        ![temperatures isKindOfClass:NSArray.class] || temperatures.count != 3) return NO;

    BOOL baseline = [result[@"Ftst"] unsignedIntegerValue] == 0;
    for (NSUInteger index = 0; index < 2; ++index) {
        if (![mode[index] isKindOfClass:NSNumber.class] ||
            ![target[index] isKindOfClass:NSNumber.class] ||
            ![actual[index] isKindOfClass:NSNumber.class]) return NO;
        double goal = [target[index] doubleValue];
        double rpm = [actual[index] doubleValue];
        if (!isfinite(goal) || !isfinite(rpm) || goal < 0 || rpm < 0) return NO;
        baseline = baseline && [mode[index] unsignedIntegerValue] == 3 && fabs(goal) <= 1.0;
    }
    for (NSNumber *temperature in temperatures) {
        if (![temperature isKindOfClass:NSNumber.class] ||
            !isfinite(temperature.doubleValue)) return NO;
    }
    return [result[@"baseline"] isEqual:@(baseline)];
}
