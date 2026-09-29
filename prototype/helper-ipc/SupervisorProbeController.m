#import "SupervisorProbeController.h"
#import "HelperSupervisorValidation.h"
#import <Security/Security.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

@interface SupervisorProbeController ()
@property(nonatomic, strong) NSTask *task;
@property(nonatomic, copy) NSString *state;
@property(nonatomic, copy) NSString *reason;
@property(nonatomic, copy) NSDictionary *report;
@property(nonatomic) pid_t runnerPID;
@end

// Resolves only the sibling of this daemon, then pins Apple anchor, identifier
// and the daemon's own Team ID. The XPC caller cannot supply a path or arguments.
NSURL *SupervisorProbeRunnerURL(void) {
    SecCodeRef selfCode = NULL;
    CFDictionaryRef info = NULL;
    if (SecCodeCopySelf(kSecCSDefaultFlags, &selfCode) != errSecSuccess) return nil;
    OSStatus copied = SecCodeCopySigningInformation(selfCode, kSecCSSigningInformation, &info);
    NSDictionary *values = (__bridge NSDictionary *)info;
    NSString *team = copied == errSecSuccess ? [values[(__bridge NSString *)kSecCodeInfoTeamIdentifier] copy] : nil;
    if (info) CFRelease(info);
    CFRelease(selfCode);
    if (team.length != 10 || [team rangeOfCharacterFromSet:
        [[NSCharacterSet characterSetWithCharactersInString:@"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"] invertedSet]].location != NSNotFound) return nil;
    char executable[PATH_MAX], canonical[PATH_MAX];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) != 0 || !realpath(executable, canonical)) return nil;
    NSString *path = [[NSString stringWithUTF8String:canonical].stringByDeletingLastPathComponent
        stringByAppendingPathComponent:@"supervisor-probe"];
    NSURL *url = [NSURL fileURLWithPath:path];
    NSString *rule = [NSString stringWithFormat:
        @"anchor apple generic and identifier \"com.ventilator.helper-ipc.signed-supervisor\" and certificate leaf[subject.OU] = \"%@\"", team];
    SecRequirementRef requirement = NULL;
    SecStaticCodeRef code = NULL;
    BOOL valid = SecRequirementCreateWithString((__bridge CFStringRef)rule, kSecCSDefaultFlags, &requirement) == errSecSuccess &&
        SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags, &code) == errSecSuccess &&
        SecStaticCodeCheckValidity(code, kSecCSStrictValidate | kSecCSCheckAllArchitectures, requirement) == errSecSuccess;
    if (code) CFRelease(code);
    if (requirement) CFRelease(requirement);
    return valid ? url : nil;
}

@implementation SupervisorProbeController
+ (instancetype)shared {
    static SupervisorProbeController *controller;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ controller = [SupervisorProbeController new]; });
    return controller;
}

- (instancetype)init {
    self = [super init];
    if (self) _state = @"idle";
    return self;
}

- (NSDictionary *)statusLocked {
    NSMutableDictionary *result = [@{@"protocol_version": @(HelperStatusProtocolVersion),
        @"backend": @"read_only", @"write_available": @NO, @"daemon_pid": @(getpid()),
        @"runner_pid": @(_runnerPID), @"state": _state} mutableCopy];
    if (_report) result[@"report"] = _report;
    if (_reason) result[@"reason"] = _reason;
    return result;
}

- (NSDictionary *)status { @synchronized (self) { return [self statusLocked]; } }

- (NSDictionary *)launchLocked:(BOOL)cleanup {
    _report = nil;
    _reason = nil;
    _runnerPID = 0;
    if (getuid() != 0 || geteuid() != 0) {
        _state = @"failed"; _reason = @"permission";
        return [self statusLocked];
    }
    NSURL *runner = SupervisorProbeRunnerURL();
    if (!runner) {
        _state = @"failed"; _reason = @"signature";
        return [self statusLocked];
    }
    NSTask *task = [NSTask new];
    task.executableURL = runner;
    task.arguments = cleanup ? @[@"--cleanup"] : @[];
    task.environment = @{};
    task.standardInput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    NSPipe *output = [NSPipe pipe];
    task.standardOutput = output;
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) {
        _state = @"failed"; _reason = @"launch";
        return [self statusLocked];
    }
    _task = task;
    _runnerPID = task.processIdentifier;
    _state = @"running";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSMutableData *data = [NSMutableData data];
        BOOL tooLarge = NO;
        for (;;) {
            NSData *part = [output.fileHandleForReading readDataOfLength:4096];
            if (part.length == 0) break;
            if (data.length + part.length > 4096) { tooLarge = YES; [task terminate]; break; }
            [data appendData:part];
        }
        [task waitUntilExit];
        [output.fileHandleForReading closeFile];
        id result = !tooLarge ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        @synchronized (self) {
            self.task = nil;
            if (tooLarge) { self.state = @"failed"; self.reason = @"output"; }
            else if (task.terminationReason != NSTaskTerminationReasonExit || task.terminationStatus != 0) {
                self.state = @"failed"; self.reason = @"exit";
            } else if (cleanup && [result isEqual:@{@"cleaned": @YES}]) self.state = @"cleaned";
            else if (!cleanup && HelperSupervisorReportValid(result) &&
                     [result[@"runner_pid"] isEqual:@(self.runnerPID)]) {
                self.state = @"finished"; self.report = result;
            } else { self.state = @"failed"; self.reason = @"contract"; }
        }
    });
    return [self statusLocked];
}

- (NSDictionary *)start {
    @synchronized (self) {
        if (![_state isEqual:@"idle"] && ![_state isEqual:@"cleaned"]) return [self statusLocked];
        return [self launchLocked:NO];
    }
}

- (NSDictionary *)cleanup {
    @synchronized (self) {
        if (_task) return [self statusLocked];
        return [self launchLocked:YES];
    }
}
@end
