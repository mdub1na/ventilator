#import <Foundation/Foundation.h>
#import "SmcBaselineRead.h"

static inline id HelperTemperatureValue(double celsius) {
    return smc_baseline_temperature_valid(celsius) ? @(celsius) : [NSNull null];
}

static inline BOOL HelperTemperatureValueValid(id value) {
    return value == [NSNull null] ||
        ([value isKindOfClass:NSNumber.class] &&
         smc_baseline_temperature_valid([value doubleValue]));
}
