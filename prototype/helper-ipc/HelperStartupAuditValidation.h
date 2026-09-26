#import "HelperBaselineValidation.h"

static inline BOOL HelperStartupAuditResponseValid(NSDictionary<NSString *, id> *result) {
    if (![result isKindOfClass:NSDictionary.class] ||
        ![result[@"protocol_version"] isEqual:@(HelperStatusProtocolVersion)] ||
        ![result[@"daemon_pid"] isKindOfClass:NSNumber.class] ||
        [result[@"daemon_pid"] intValue] <= 0 ||
        ![result[@"control_allowed"] isEqual:@NO] ||
        ![result[@"state"] isKindOfClass:NSString.class]) return NO;

    NSString *state = result[@"state"];
    if ([state isEqual:@"read_failed"]) {
        NSSet<NSString *> *reasons = [NSSet setWithArray:@[
            @"unsupported_environment", @"open_failed", @"read_failed",
            @"unexpected_format"
        ]];
        return result.count == 6 && [result[@"available"] isEqual:@NO] &&
            [result[@"reason"] isKindOfClass:NSString.class] &&
            [reasons containsObject:result[@"reason"]];
    }
    if (!([state isEqual:@"system_at_start"] || [state isEqual:@"changed_at_start"]) ||
        result.count != 14) return NO;
    NSMutableDictionary<NSString *, id> *snapshot = [result mutableCopy];
    [snapshot removeObjectsForKeys:@[@"state", @"daemon_pid", @"control_allowed"]];
    if (!HelperBaselineResponseValid(snapshot)) return NO;
    return [state isEqual:@"system_at_start"] == [result[@"baseline"] boolValue];
}
