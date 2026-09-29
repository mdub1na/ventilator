#import "HelperStatus.h"
#include <math.h>
#include <limits.h>

static inline BOOL SupervisorUInt(id value, uint64_t maximum) {
    return [value isKindOfClass:NSNumber.class] &&
        CFGetTypeID((__bridge CFTypeRef)value) != CFBooleanGetTypeID() &&
        isfinite([value doubleValue]) && [value doubleValue] >= 0 &&
        [value unsignedLongLongValue] <= maximum &&
        [value isEqual:@([value unsignedLongLongValue])];
}

static inline BOOL SupervisorBool(id value) {
    return [value isKindOfClass:NSNumber.class] &&
        CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
}

static inline BOOL HelperSupervisorCrashMarkerValid(NSDictionary *marker, pid_t runner, uint64_t now) {
    if (![marker isKindOfClass:NSDictionary.class] || marker.count != 5 || runner <= 0 ||
        !SupervisorUInt(marker[@"runner_pid"], INT_MAX) ||
        [marker[@"runner_pid"] intValue] != runner ||
        !SupervisorUInt(marker[@"runner_uid"], 0) ||
        !SupervisorUInt(marker[@"observer_pid"], INT_MAX) ||
        [marker[@"observer_pid"] intValue] <= 0 || [marker[@"observer_pid"] intValue] == runner ||
        !SupervisorBool(marker[@"journal_pending"]) || ![marker[@"journal_pending"] boolValue] ||
        !SupervisorUInt(marker[@"armed_monotonic_ns"], UINT64_MAX)) return NO;
    uint64_t armed = [marker[@"armed_monotonic_ns"] unsignedLongLongValue];
    return armed > 0 && now >= armed && now - armed <= UINT64_C(2000000000);
}

static inline BOOL HelperSupervisorReportValid(NSDictionary *report) {
    if (![report isKindOfClass:NSDictionary.class] || report.count != 14 ||
        ![report[@"state"] isKindOfClass:NSString.class] ||
        !SupervisorUInt(report[@"runner_uid"], 0) ||
        !SupervisorUInt(report[@"runner_pid"], INT_MAX) || [report[@"runner_pid"] intValue] == 0 ||
        !SupervisorBool(report[@"resumed"]) || !SupervisorBool(report[@"journal_clear"])) return NO;
    NSArray *pids = @[@"admission_pid", @"operation_pid", @"recovery_pid", @"observer_pid"];
    for (NSString *key in pids) if (!SupervisorUInt(report[key], INT_MAX)) return NO;
    for (NSString *key in @[@"admission_samples", @"recovery_samples"])
        if (!SupervisorUInt(report[key], 61)) return NO;
    for (NSString *key in @[@"admission_duration_ns", @"recovery_duration_ns"])
        if (!SupervisorUInt(report[key], UINT64_C(75000000000))) return NO;
    if (!SupervisorUInt(report[@"reaped"], 4)) return NO;
    NSString *state = report[@"state"];
    BOOL clear = [report[@"journal_clear"] boolValue];
    if ([state isEqual:@"blocked"] || [state isEqual:@"pending"])
        return clear == [state isEqual:@"blocked"] && [report[@"recovery_samples"] intValue] == 0 &&
            [report[@"recovery_duration_ns"] unsignedLongLongValue] == 0;
    if (![state isEqual:@"verified"] || !clear ||
        [report[@"recovery_samples"] intValue] != 61 ||
        [report[@"recovery_duration_ns"] unsignedLongLongValue] < UINT64_C(60000000000)) return NO;
    BOOL resumed = [report[@"resumed"] boolValue];
    if (resumed) {
        if ([report[@"admission_pid"] intValue] != 0 || [report[@"operation_pid"] intValue] != 0 ||
            [report[@"admission_samples"] intValue] != 0 ||
            [report[@"admission_duration_ns"] unsignedLongLongValue] != 0 ||
            [report[@"reaped"] intValue] != 2) return NO;
    } else if ([report[@"admission_samples"] intValue] != 61 ||
               [report[@"admission_duration_ns"] unsignedLongLongValue] < UINT64_C(60000000000) ||
               [report[@"reaped"] intValue] != 4) return NO;
    NSMutableSet *seen = [NSMutableSet setWithObject:report[@"runner_pid"]];
    for (NSString *key in pids) {
        NSNumber *pid = report[key];
        if (resumed && ([key isEqual:@"admission_pid"] || [key isEqual:@"operation_pid"])) continue;
        if (pid.intValue == 0 || [seen containsObject:pid]) return NO;
        [seen addObject:pid];
    }
    return YES;
}

static inline BOOL HelperSupervisorResponseValid(NSDictionary *response) {
    if (![response isKindOfClass:NSDictionary.class] ||
        ![response[@"protocol_version"] isEqual:@(HelperStatusProtocolVersion)] ||
        ![response[@"backend"] isEqual:@"read_only"] ||
        !SupervisorBool(response[@"write_available"]) || [response[@"write_available"] boolValue] ||
        !SupervisorUInt(response[@"daemon_pid"], INT_MAX) || [response[@"daemon_pid"] intValue] == 0 ||
        !SupervisorUInt(response[@"runner_pid"], INT_MAX) ||
        ![response[@"state"] isKindOfClass:NSString.class]) return NO;
    NSString *state = response[@"state"];
    if ([state isEqual:@"interrupted"]) {
        NSDictionary *crash = response[@"crash"];
        return response.count == 7 && [response[@"runner_pid"] intValue] > 0 &&
            [crash isKindOfClass:NSDictionary.class] && crash.count == 3 &&
            SupervisorUInt(crash[@"observer_pid"], INT_MAX) && [crash[@"observer_pid"] intValue] > 0 &&
            ![crash[@"observer_pid"] isEqual:response[@"runner_pid"]] &&
            ![crash[@"observer_pid"] isEqual:response[@"daemon_pid"]] &&
            SupervisorBool(crash[@"journal_pending"]) && [crash[@"journal_pending"] boolValue] &&
            SupervisorUInt(crash[@"loss_duration_ns"], UINT64_C(2000000000));
    }
    if ([state isEqual:@"finished"])
        return response.count == 7 && HelperSupervisorReportValid(response[@"report"]) &&
            [response[@"runner_pid"] isEqual:response[@"report"][@"runner_pid"]];
    if ([state isEqual:@"failed"])
        return response.count == 7 &&
            [@[@"permission", @"signature", @"launch", @"exit", @"contract", @"output", @"owner_lost", @"busy"] containsObject:response[@"reason"]];
    if ([state isEqual:@"running"]) return response.count == 6 && [response[@"runner_pid"] intValue] > 0;
    return response.count == 6 && ([state isEqual:@"idle"] || [state isEqual:@"cleaned"]);
}

// A held diagnostic connection must not auto-reconnect into a different daemon
// or accept a different runner's terminal result after interruption/restart.
static inline BOOL HelperSupervisorSameRunValid(NSDictionary *started, NSDictionary *response) {
    return HelperSupervisorResponseValid(started) && HelperSupervisorResponseValid(response) &&
        [started[@"daemon_pid"] isEqual:response[@"daemon_pid"]] &&
        [started[@"runner_pid"] isEqual:response[@"runner_pid"]];
}
