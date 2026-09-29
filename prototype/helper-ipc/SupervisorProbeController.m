#import "SupervisorProbeController.h"
#import "HelperSupervisorValidation.h"
#import <Security/Security.h>
#include "SupervisorProbeDirectory.h"
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

typedef enum { ProbeRun, ProbeCleanup, ProbeCrash, ProbeResume } SupervisorProbeMode;

@interface SupervisorProbeController ()
@property(nonatomic, strong) NSTask *task;
@property(nonatomic, copy) NSString *state;
@property(nonatomic, copy) NSString *reason;
@property(nonatomic, copy) NSDictionary *report;
@property(nonatomic, copy) NSDictionary *crash;
@property(nonatomic) pid_t runnerPID;
@end

// Resolves only the sibling of this daemon, then pins Apple anchor, identifier
// and the daemon's own Team ID. The XPC caller cannot supply a path or arguments.
static NSString *ownTeamID(void) {
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
    return team;
}

static BOOL validSignature(NSURL *url) {
    NSString *team = ownTeamID();
    if (!team) return NO;
    NSString *rule = [NSString stringWithFormat:
        @"anchor apple generic and identifier \"com.ventilator.helper-ipc.signed-supervisor\" and certificate leaf[subject.OU] = \"%@\"", team];
    SecRequirementRef requirement = NULL;
    SecStaticCodeRef code = NULL;
    BOOL valid = SecRequirementCreateWithString((__bridge CFStringRef)rule, kSecCSDefaultFlags, &requirement) == errSecSuccess &&
        SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags, &code) == errSecSuccess &&
        SecStaticCodeCheckValidity(code, kSecCSStrictValidate | kSecCSCheckAllArchitectures, requirement) == errSecSuccess;
    if (code) CFRelease(code);
    if (requirement) CFRelease(requirement);
    return valid;
}

static NSURL *siblingRunnerURL(void) {
    char executable[PATH_MAX], canonical[PATH_MAX];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) != 0 || !realpath(executable, canonical)) return nil;
    NSString *path = [[NSString stringWithUTF8String:canonical].stringByDeletingLastPathComponent
        stringByAppendingPathComponent:@"supervisor-probe"];
    return [NSURL fileURLWithPath:path];
}

NSURL *SupervisorProbeRunnerURL(void) {
    NSURL *url = siblingRunnerURL();
    return url && validSignature(url) ? url : nil;
}

static NSURL *stageRunner(NSURL *sourceURL, int directory, NSURL *directoryURL) {
    // The app bundle is user owned. Verify COPIED bytes inside root-owned 0700
    // storage before exec so replacing the source after checking cannot replace
    // code that NSTask will execute with root privileges.
    struct stat folder = {0};
    if (!sourceURL || fstat(directory, &folder) != 0 || !S_ISDIR(folder.st_mode) ||
        folder.st_uid != geteuid() || (folder.st_mode & 0777) != 0700) return nil;
    int source = open(sourceURL.fileSystemRepresentation, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat info = {0};
    if (source < 0) return nil;
    if (fstat(source, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > 8 * 1024 * 1024) {
        close(source); return nil;
    }
    int output = openat(directory, SupervisorProbeStagingName, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    BOOL copied = output >= 0;
    off_t remaining = info.st_size;
    char buffer[16384];
    while (copied && remaining > 0) {
        ssize_t count = read(source, buffer, remaining < (off_t)sizeof(buffer) ? (size_t)remaining : sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { copied = NO; break; }
        remaining -= count;
        ssize_t written = 0;
        while (written < count) {
            ssize_t chunk = write(output, buffer + written, (size_t)(count - written));
            if (chunk < 0 && errno == EINTR) continue;
            if (chunk <= 0) { copied = NO; break; }
            written += chunk;
        }
    }
    copied = copied && fchmod(output, 0500) == 0 && fsync(output) == 0;
    if (output >= 0 && close(output) != 0) copied = NO;
    close(source);
    NSString *path = directoryURL.path;
    NSURL *staged = [NSURL fileURLWithPath:[path stringByAppendingPathComponent:
        [NSString stringWithUTF8String:SupervisorProbeStagingName]]];
    BOOL trusted = copied && validSignature(staged);
    NSURL *runner = nil;
    if (trusted && renameat(directory, SupervisorProbeStagingName, directory, SupervisorProbeExecutableName) == 0 && fsync(directory) == 0)
        runner = [NSURL fileURLWithPath:[path stringByAppendingPathComponent:
            [NSString stringWithUTF8String:SupervisorProbeExecutableName]]];
    if (output >= 0) (void)unlinkat(directory, SupervisorProbeStagingName, 0);
    return runner;
}

BOOL SupervisorProbeCopySignatureCheck(void) {
    // Fixed signature-only diagnostic: a disposable owned fixture, no exec,
    // no root state and no caller-supplied path. Exercises validation AFTER copy.
    char path[] = "/private/tmp/ventilator-supervisor-copy-test.XXXXXX";
    if (!mkdtemp(path)) return NO;
    int directory = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    BOOL valid = directory >= 0 && stageRunner(siblingRunnerURL(), directory,
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]]) != nil;
    if (directory >= 0) {
        (void)unlinkat(directory, SupervisorProbeExecutableName, 0);
        (void)unlinkat(directory, SupervisorProbeStagingName, 0);
        (void)close(directory);
    }
    if (rmdir(path) != 0) valid = NO;
    return valid;
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
    if (_crash) result[@"crash"] = _crash;
    if (_reason) result[@"reason"] = _reason;
    return result;
}

- (NSDictionary *)status { @synchronized (self) { return [self statusLocked]; } }

- (NSDictionary *)launchLocked:(SupervisorProbeMode)mode {
    BOOL cleanup = mode == ProbeCleanup;
    _report = nil;
    _crash = nil;
    _reason = nil;
    _runnerPID = 0;
    if (getuid() != 0 || geteuid() != 0) {
        _state = @"failed"; _reason = @"permission";
        return [self statusLocked];
    }
    NSURL *runner = SupervisorProbeRunnerURL();
    if (runner) {
        int directory = SupervisorProbeOpenDirectory();
        runner = directory >= 0 ? stageRunner(runner, directory,
            [NSURL fileURLWithPath:[NSString stringWithUTF8String:SupervisorProbeDirectoryPath]]) : nil;
        if (directory >= 0) close(directory);
    }
    if (!runner) {
        _state = @"failed"; _reason = @"signature";
        return [self statusLocked];
    }
    NSTask *task = [NSTask new];
    task.executableURL = runner;
    task.arguments = cleanup ? @[@"--cleanup"] : mode == ProbeCrash ? @[@"--crash-observer"] :
        mode == ProbeResume ? @[@"--resume-pending"] : @[];
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
        uint64_t finished = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
        id result = !tooLarge ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        @synchronized (self) {
            self.task = nil;
            if (tooLarge) { self.state = @"failed"; self.reason = @"output"; }
            else if (mode == ProbeCrash && task.terminationReason == NSTaskTerminationReasonUncaughtSignal &&
                     task.terminationStatus == SIGKILL &&
                     HelperSupervisorCrashMarkerValid(result, self.runnerPID, finished)) {
                self.state = @"interrupted";
                self.crash = @{@"observer_pid": result[@"observer_pid"], @"journal_pending": @YES,
                    @"loss_duration_ns": @(finished - [result[@"armed_monotonic_ns"] unsignedLongLongValue])};
            }
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
        return [self launchLocked:ProbeRun];
    }
}

- (NSDictionary *)startCrash {
    @synchronized (self) {
        if (![_state isEqual:@"idle"] && ![_state isEqual:@"cleaned"]) return [self statusLocked];
        return [self launchLocked:ProbeCrash];
    }
}

- (NSDictionary *)resume {
    @synchronized (self) {
        if (_task) return [self statusLocked];
        return [self launchLocked:ProbeResume];
    }
}

- (NSDictionary *)cleanup {
    @synchronized (self) {
        if (_task) return [self statusLocked];
        return [self launchLocked:ProbeCleanup];
    }
}
@end
