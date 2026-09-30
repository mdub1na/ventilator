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

static void interrupted_requires_bounded_root_evidence(void) {
    NSMutableDictionary *marker = [@{@"runner_pid": @100, @"runner_uid": @0, @"observer_pid": @104,
        @"journal_pending": @YES, @"armed_monotonic_ns": @1000000000ULL} mutableCopy];
    NSCAssert(HelperSupervisorCrashMarkerValid(marker, 100, 1000000001ULL), @"valid crash marker");
    NSCAssert(!HelperSupervisorCrashMarkerValid(marker, 101, 1000000001ULL), @"other runner");
    NSCAssert(!HelperSupervisorCrashMarkerValid(marker, 100, 999999999ULL), @"future marker");
    NSCAssert(!HelperSupervisorCrashMarkerValid(marker, 100, 3000000001ULL), @"late fallback EOF");
    NSDictionary *invalid = @{@"runner_uid": @501, @"runner_pid": @YES, @"observer_pid": @100,
        @"journal_pending": @NO, @"armed_monotonic_ns": @0};
    for (NSString *key in invalid) {
        NSMutableDictionary *bad = [marker mutableCopy];
        bad[key] = invalid[key];
        NSCAssert(!HelperSupervisorCrashMarkerValid(bad, 100, 1000000001ULL), @"bad marker %@", key);
    }
    NSMutableDictionary *response = [@{@"protocol_version": @(HelperStatusProtocolVersion),
        @"backend": @"read_only", @"write_available": @NO, @"daemon_pid": @99, @"runner_pid": @100,
        @"state": @"interrupted", @"crash": @{@"observer_pid": @104, @"journal_pending": @YES,
                                               @"loss_duration_ns": @1000000}} mutableCopy];
    NSCAssert(HelperSupervisorResponseValid(response), @"bounded interrupted status");
    for (NSDictionary *bad in @[@{@"observer_pid": @100, @"journal_pending": @YES, @"loss_duration_ns": @1},
                                @{@"observer_pid": @99, @"journal_pending": @YES, @"loss_duration_ns": @1},
                                @{@"observer_pid": @104, @"journal_pending": @NO, @"loss_duration_ns": @1},
                                @{@"observer_pid": @104, @"journal_pending": @YES, @"loss_duration_ns": @2000000001ULL},
                                @{@"observer_pid": @104, @"journal_pending": @YES, @"loss_duration_ns": @YES}]) {
        response[@"crash"] = bad;
        NSCAssert(!HelperSupervisorResponseValid(response), @"invalid interruption evidence");
    }
    response[@"state"] = @"running";
    NSCAssert(!HelperSupervisorResponseValid(response), @"running cannot carry old crash evidence");
}

static void resume_requires_a_new_minute_and_no_repeated_operation(void) {
    NSMutableDictionary *report = verifiedReport();
    report[@"resumed"] = @YES;
    report[@"admission_pid"] = @0;
    report[@"admission_samples"] = @0;
    report[@"admission_duration_ns"] = @0;
    report[@"operation_pid"] = @0;
    report[@"reaped"] = @2;
    NSCAssert(HelperSupervisorReportValid(report), @"valid recovery-only report");
    for (NSString *key in @[@"operation_pid", @"admission_pid", @"admission_samples", @"admission_duration_ns"]) {
        NSMutableDictionary *bad = [report mutableCopy]; bad[key] = @1;
        NSCAssert(!HelperSupervisorReportValid(bad), @"resume must not repeat %@", key);
    }
    report[@"recovery_duration_ns"] = @59999999999ULL;
    NSCAssert(!HelperSupervisorReportValid(report), @"resume needs its own full minute");
}

static void reconnect_or_different_run_is_not_a_terminal_result(void) {
    NSMutableDictionary *started = [@{@"protocol_version": @(HelperStatusProtocolVersion),
        @"backend": @"read_only", @"write_available": @NO, @"daemon_pid": @99,
        @"runner_pid": @100, @"state": @"running"} mutableCopy];
    NSMutableDictionary *finished = [started mutableCopy];
    finished[@"state"] = @"finished"; finished[@"report"] = verifiedReport();
    NSCAssert(HelperSupervisorSameRunValid(started, finished), @"same live run result");
    finished[@"daemon_pid"] = @98;
    NSCAssert(!HelperSupervisorSameRunValid(started, finished), @"new daemon cannot reuse old request");
    finished[@"daemon_pid"] = @99;
    NSMutableDictionary *other = [started mutableCopy]; other[@"runner_pid"] = @200;
    NSCAssert(!HelperSupervisorSameRunValid(started, other), @"different runner");
    other = [started mutableCopy]; other[@"state"] = @"failed"; other[@"reason"] = @"owner_lost";
    NSCAssert(HelperSupervisorSameRunValid(started, other), @"owner loss is a failure status");
    other[@"report"] = verifiedReport();
    NSCAssert(!HelperSupervisorSameRunValid(started, other), @"lost owner cannot carry proof");
}

int main(void) {
    @autoreleasepool {
        NSCAssert(HelperSupervisorReportValid(verifiedReport()), @"valid pair of minutes must pass");
        false_success_is_rejected();
        xpc_write_or_mismatched_runner_is_rejected();
        interrupted_requires_bounded_root_evidence();
        resume_requires_a_new_minute_and_no_repeated_operation();
        reconnect_or_different_run_is_not_a_terminal_result();
        puts("read-only supervisor contract tests passed");
    }
}
