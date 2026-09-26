#import "StartupAuditController.h"
#import "HelperStartupAuditValidation.h"

static unsigned reads;

static SmcBaselineResult systemReader(SmcBaselineSnapshot *snapshot) {
    ++reads;
    snapshot->mode[0] = 3;
    snapshot->mode[1] = 3;
    snapshot->temperatures_c[0] = 50;
    snapshot->temperatures_c[1] = 40;
    snapshot->temperatures_c[2] = 30;
    return SMC_BASELINE_OK;
}

static SmcBaselineResult changedReader(SmcBaselineSnapshot *snapshot) {
    ++reads;
    snapshot->ftst = 1;
    snapshot->target_rpm[0] = 1350;
    snapshot->target_rpm[1] = 1458;
    snapshot->temperatures_c[0] = 50;
    snapshot->temperatures_c[1] = 40;
    snapshot->temperatures_c[2] = 30;
    return SMC_BASELINE_OK;
}

static SmcBaselineResult failingReader(SmcBaselineSnapshot *snapshot) {
    (void)snapshot;
    ++reads;
    return SMC_BASELINE_READ_FAILED;
}

int main(void) {
    @autoreleasepool {
        reads = 0;
        StartupAuditController *system = [[StartupAuditController alloc] initWithReader:systemReader];
        NSDictionary *systemStatus = system.status;
        NSCAssert(reads == 1 && system.status == systemStatus,
                  @"startup audit must capture exactly once");
        NSCAssert([systemStatus[@"state"] isEqual:@"system_at_start"] &&
                  [systemStatus[@"control_allowed"] isEqual:@NO] &&
                  HelperStartupAuditResponseValid(systemStatus),
                  @"one system snapshot must be valid without authorizing control");

        reads = 0;
        StartupAuditController *changed = [[StartupAuditController alloc] initWithReader:changedReader];
        NSDictionary *changedStatus = changed.status;
        NSCAssert(reads == 1 && [changedStatus[@"state"] isEqual:@"changed_at_start"] &&
                  [changedStatus[@"baseline"] isEqual:@NO] &&
                  HelperStartupAuditResponseValid(changedStatus),
                  @"changed startup state must remain visible");
        NSMutableDictionary *forged = [changedStatus mutableCopy];
        forged[@"state"] = @"system_at_start";
        NSCAssert(!HelperStartupAuditResponseValid(forged),
                  @"client must reject a false system claim");
        forged = [systemStatus mutableCopy];
        forged[@"control_allowed"] = @YES;
        NSCAssert(!HelperStartupAuditResponseValid(forged),
                  @"startup snapshot must never authorize control");

        reads = 0;
        StartupAuditController *failed = [[StartupAuditController alloc] initWithReader:failingReader];
        NSDictionary *failedStatus = failed.status;
        NSCAssert(reads == 1 && [failedStatus[@"state"] isEqual:@"read_failed"] &&
                  [failedStatus[@"available"] isEqual:@NO] &&
                  failedStatus[@"Ftst"] == nil &&
                  HelperStartupAuditResponseValid(failedStatus),
                  @"failed read must not expose a fabricated baseline");
        puts("startup audit tests passed");
        return 0;
    }
}
