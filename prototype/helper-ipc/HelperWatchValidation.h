#import "HelperStatus.h"
#import "HelperTemperatureValue.h"
#include <math.h>

static inline BOOL HelperWatchResponseValid(NSDictionary<NSString *, id> *result) {
    if (![result isKindOfClass:NSDictionary.class] ||
        ![result[@"protocol_version"] isEqual:@(HelperStatusProtocolVersion)] ||
        ![result[@"state"] isKindOfClass:NSString.class] ||
        ![result[@"samples"] isKindOfClass:NSNumber.class] ||
        ![result[@"last_second"] isKindOfClass:NSNumber.class] ||
        ![result[@"daemon_pid"] isKindOfClass:NSNumber.class] ||
        [result[@"daemon_pid"] intValue] <= 0 ||
        ![result[@"started_monotonic_ns"] isKindOfClass:NSNumber.class] ||
        ![result[@"last_sample_monotonic_ns"] isKindOfClass:NSNumber.class]) return NO;
    NSString *state = result[@"state"];
    unsigned samples = [result[@"samples"] unsignedIntValue];
    unsigned second = [result[@"last_second"] unsignedIntValue];
    if ([state isEqual:@"idle"])
        return result.count == 7 && samples == 0 && second == 0;
    if ([state isEqual:@"read_failed"])
        return result.count == 8 && [result[@"reason"] isKindOfClass:NSString.class] &&
               samples <= 60 && second <= 59;
    if (!([state isEqual:@"running"] || [state isEqual:@"stable"] ||
          [state isEqual:@"changed"]) || result.count != 13 ||
        samples == 0 || samples > 61 || second != samples - 1 ||
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
    for (id temperature in temperatures) {
        if (!HelperTemperatureValueValid(temperature)) return NO;
    }
    if (![result[@"baseline"] isEqual:@(baseline)]) return NO;
    if ([state isEqual:@"running"])
        return baseline && second < 60;
    if ([state isEqual:@"stable"])
        return baseline && samples == 61 && second == 60;
    return !baseline;
}
