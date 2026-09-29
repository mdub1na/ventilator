#import "HelperSupervisorValidation.h"

static NSMutableDictionary *verifiedReport(void) {
    return [@{@"state": @"verified", @"runner_pid": @100, @"runner_uid": @0, @"resumed": @NO,
        @"admission_pid": @101, @"admission_samples": @61, @"admission_duration_ns": @60000000000ULL,
        @"operation_pid": @102, @"recovery_pid": @103, @"observer_pid": @104, @"reaped": @4,
        @"recovery_samples": @61, @"recovery_duration_ns": @60000000000ULL, @"journal_clear": @YES} mutableCopy];
}

static void false_success_is_rejected(void) {
    NSDictionary *invalid = @{@"runner_uid": @501, @"runner_pid": @0, @"admission_samples": @60,
        @"admission_duration_ns": @59999999999ULL, @"recovery_samples": @60,
        @"recovery_duration_ns": @59999999999ULL, @"journal_clear": @NO, @"operation_pid": @101,
        @"reaped": @3, @"resumed": @YES, @"observer_pid": @100};
    for (NSString *key in invalid) {
        NSMutableDictionary *report = verifiedReport();
        report[key] = invalid[key];
        NSCAssert(!HelperSupervisorReportValid(report), @"false success for %@", key);
    }
    for (id value in @[@YES, @(-1), @1.5, @2147483648ULL, @"103"]) {
        NSMutableDictionary *report = verifiedReport();
        report[@"recovery_pid"] = value;
        NSCAssert(!HelperSupervisorReportValid(report), @"invalid PID must fail");
    }
    NSMutableDictionary *report = verifiedReport();
    report[@"extra"] = @1;
    NSCAssert(!HelperSupervisorReportValid(report), @"extra report field must fail");
}

static void xpc_write_or_mismatched_runner_is_rejected(void) {
    NSMutableDictionary *response = [@{@"protocol_version": @(HelperStatusProtocolVersion),
        @"backend": @"read_only", @"write_available": @NO, @"daemon_pid": @99, @"runner_pid": @100,
        @"state": @"finished", @"report": verifiedReport()} mutableCopy];
    NSCAssert(HelperSupervisorResponseValid(response), @"valid report must pass");
    response[@"write_available"] = @YES;
    NSCAssert(!HelperSupervisorResponseValid(response), @"write capability must fail");
    response[@"write_available"] = @NO;
    response[@"runner_pid"] = @101;
    NSCAssert(!HelperSupervisorResponseValid(response), @"other runner must fail");
    response[@"state"] = @"running";
    NSCAssert(!HelperSupervisorResponseValid(response), @"running cannot carry an old report");
    [response removeObjectForKey:@"report"];
    NSCAssert(HelperSupervisorResponseValid(response), @"running status must pass");
    response[@"backend"] = @"hardware_writer";
    NSCAssert(!HelperSupervisorResponseValid(response), @"another backend must fail");
}

int main(void) {
    @autoreleasepool {
        NSCAssert(HelperSupervisorReportValid(verifiedReport()), @"valid pair of minutes must pass");
        false_success_is_rejected();
        xpc_write_or_mismatched_runner_is_rejected();
        puts("read-only supervisor contract tests passed");
    }
}
