#import "HelperStatus.h"
#import "HelperBaselineValidation.h"
#import "HelperWatchValidation.h"
#import "HelperStartupAuditValidation.h"
#import "HelperSupervisorValidation.h"
#import <Security/Security.h>
#import <ServiceManagement/ServiceManagement.h>
#include <jni.h>
#include <stdio.h>
#include <time.h>

static NSString *const ServiceName = @"com.ventilator.helper-ipc.read-only";
static NSString *const PlistName = @"com.ventilator.helper-ipc.read-only.plist";
static NSString *const DaemonIdentifier = @"com.ventilator.helper-ipc.signed-daemon";
static NSString *const AppIdentifier = @"ventilator.desktop";

static void throwFailure(JNIEnv *environment, NSString *message) {
    jclass exceptionClass = (*environment)->FindClass(environment, "java/lang/IllegalStateException");
    if (exceptionClass) {
        (*environment)->ThrowNew(environment, exceptionClass,
            (message ?: @"Unknown helper probe error").UTF8String);
    }
}

static NSString *ownTeamID(void) {
    if (![NSBundle.mainBundle.bundleIdentifier isEqualToString:AppIdentifier]) return nil;
    SecCodeRef code = NULL;
    CFDictionaryRef information = NULL;
    if (SecCodeCopySelf(kSecCSDefaultFlags, &code) != errSecSuccess) return nil;
    OSStatus status = SecCodeCopySigningInformation(code, kSecCSSigningInformation, &information);
    NSString *team = nil;
    if (status == errSecSuccess && information) {
        NSDictionary *values = (__bridge NSDictionary *)information;
        team = [values[(__bridge NSString *)kSecCodeInfoTeamIdentifier] copy];
    }
    if (information) CFRelease(information);
    CFRelease(code);
    if (team.length != 10) return nil;
    NSCharacterSet *invalid = [[NSCharacterSet characterSetWithCharactersInString:
        @"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"] invertedSet];
    return [team rangeOfCharacterFromSet:invalid].location == NSNotFound ? team : nil;
}

static NSString *statusName(SMAppServiceStatus status) {
    switch (status) {
        case SMAppServiceStatusNotRegistered: return @"notRegistered";
        case SMAppServiceStatusEnabled: return @"enabled";
        case SMAppServiceStatusRequiresApproval: return @"requiresApproval";
        case SMAppServiceStatusNotFound: return @"notFound";
    }
    return @"unknown";
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_registrationStatusNative(
    JNIEnv *environment, jobject self
) {
    (void)self;
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService *service = [SMAppService daemonServiceWithPlistName:PlistName];
            return (*environment)->NewStringUTF(environment, statusName(service.status).UTF8String);
        }
        throwFailure(environment, @"macOS 13 or later is required");
        return NULL;
    }
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_setRegisteredNative(
    JNIEnv *environment, jobject self, jboolean registered
) {
    (void)self;
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            if (registered && !ownTeamID()) {
                throwFailure(environment, @"Ventilator.app must have a trusted development signature");
                return NULL;
            }
            SMAppService *service = [SMAppService daemonServiceWithPlistName:PlistName];
            NSError *error = nil;
            BOOL success = registered ? [service registerAndReturnError:&error] :
                [service unregisterAndReturnError:&error];
            if (!success) {
                throwFailure(environment, error.localizedDescription ?: @"Service Management failed");
                return NULL;
            }
            return (*environment)->NewStringUTF(environment, statusName(service.status).UTF8String);
        }
        throwFailure(environment, @"macOS 13 or later is required");
        return NULL;
    }
}

typedef enum {
    DaemonRequestStatus,
    DaemonRequestBaseline,
    DaemonRequestWatchStart,
    DaemonRequestWatchStatus,
    DaemonRequestStartupAudit,
    DaemonRequestSupervisorStart,
    DaemonRequestSupervisorCrashStart,
    DaemonRequestSupervisorResume,
    DaemonRequestSupervisorStatus,
    DaemonRequestSupervisorCleanup,
} DaemonRequest;

static NSDictionary<NSString *, id> *fetchDaemon(JNIEnv *environment, DaemonRequest request) {
    NSString *team = ownTeamID();
    if (!team) {
        throwFailure(environment, @"Ventilator.app must have a trusted development signature");
        return nil;
    }
    NSString *requirement = [NSString stringWithFormat:
        @"anchor apple generic and identifier \"%@\" and certificate leaf[subject.OU] = \"%@\"",
        DaemonIdentifier, team];
    NSXPCConnection *connection = [[NSXPCConnection alloc] initWithMachServiceName:ServiceName
        options:NSXPCConnectionPrivileged];
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(HelperStatusXPC)];
    [connection setCodeSigningRequirement:requirement];
    NSObject *connectionState = [NSObject new];
    __block BOOL disconnected = NO;
    connection.interruptionHandler = ^{ @synchronized (connectionState) { disconnected = YES; } };
    connection.invalidationHandler = ^{ @synchronized (connectionState) { disconnected = YES; } };
    [connection resume];

    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSDictionary<NSString *, id> *result = nil;
    __block NSError *requestError = nil;
    id<HelperStatusXPC> remote = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        requestError = error;
        dispatch_semaphore_signal(done);
    }];
    void (^complete)(NSDictionary<NSString *, id> *) = ^(NSDictionary<NSString *, id> *response) {
        result = response;
        dispatch_semaphore_signal(done);
    };
    switch (request) {
        case DaemonRequestStatus: [remote fetchStatusWithReply:complete]; break;
        case DaemonRequestBaseline: [remote fetchBaselineWithReply:complete]; break;
        case DaemonRequestWatchStart: [remote startBaselineWatchWithReply:complete]; break;
        case DaemonRequestWatchStatus: [remote fetchBaselineWatchWithReply:complete]; break;
        case DaemonRequestStartupAudit: [remote fetchStartupAuditWithReply:complete]; break;
        case DaemonRequestSupervisorStart: [remote startSupervisorProbeWithReply:complete]; break;
        case DaemonRequestSupervisorCrashStart: [remote startSupervisorCrashProbeWithReply:complete]; break;
        case DaemonRequestSupervisorResume: [remote resumeSupervisorProbeWithReply:complete]; break;
        case DaemonRequestSupervisorStatus: [remote fetchSupervisorProbeWithReply:complete]; break;
        case DaemonRequestSupervisorCleanup: [remote cleanupSupervisorProbeWithReply:complete]; break;
    }
    long timeout = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    BOOL hold = request == DaemonRequestSupervisorStart || request == DaemonRequestSupervisorCrashStart ||
                request == DaemonRequestSupervisorResume || request == DaemonRequestSupervisorCleanup;
    if (timeout == 0 && !requestError && hold && HelperSupervisorResponseValid(result) &&
        [result[@"state"] isEqual:@"running"]) {
        NSDictionary *started = result;
        // Emit the initial identity while this same signed connection remains
        // alive, so the driver can verify/interrupt its own diagnostic client.
        if (request != DaemonRequestSupervisorCleanup) {
            NSData *json = [NSJSONSerialization dataWithJSONObject:started options:NSJSONWritingSortedKeys error:nil];
            if (json) { fwrite(json.bytes, 1, json.length, stdout); fputc('\n', stdout); fflush(stdout); }
        }
        uint64_t deadline = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) + UINT64_C(180000000000);
        while ([result[@"state"] isEqual:@"running"]) {
            struct timespec delay = {.tv_nsec = 200000000};
            (void)nanosleep(&delay, NULL);
            if (clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) >= deadline) { timeout = 1; break; }
            result = nil;
            [remote fetchSupervisorProbeWithReply:complete];
            timeout = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
            if (timeout != 0 || requestError) break;
            BOOL lostConnection;
            @synchronized (connectionState) { lostConnection = disconnected; }
            if (lostConnection) {
                requestError = [NSError errorWithDomain:@"VentilatorProbe" code:2
                    userInfo:@{NSLocalizedDescriptionKey: @"Supervisor XPC connection was interrupted"}];
                break;
            }
            if (!HelperSupervisorSameRunValid(started, result)) {
                requestError = [NSError errorWithDomain:@"VentilatorProbe" code:1
                    userInfo:@{NSLocalizedDescriptionKey: @"Supervisor connection lost or run identity changed"}];
                break;
            }
        }
    }
    BOOL lostConnection;
    @synchronized (connectionState) { lostConnection = disconnected; }
    [connection invalidate];
    if (timeout != 0) {
        throwFailure(environment, @"XPC request timed out");
        return nil;
    }
    if (requestError) {
        throwFailure(environment, requestError.localizedDescription);
        return nil;
    }
    if (hold && lostConnection) {
        throwFailure(environment, @"Supervisor XPC connection was interrupted");
        return nil;
    }
    return result;
}

static jstring encodeResult(JNIEnv *environment, NSDictionary<NSString *, id> *result) {
    NSError *encodingError = nil;
    NSData *json = [NSJSONSerialization dataWithJSONObject:result
        options:NSJSONWritingSortedKeys error:&encodingError];
    if (!json) {
        throwFailure(environment, encodingError.localizedDescription ?: @"JSON encoding failed");
        return NULL;
    }
    NSString *value = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    return (*environment)->NewStringUTF(environment, value.UTF8String);
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_requestStatusNative(
    JNIEnv *environment, jobject self
) {
    (void)self;
    @autoreleasepool {
        NSDictionary<NSString *, id> *result = fetchDaemon(environment, DaemonRequestStatus);
        if (!result) return NULL;
        if (![result[@"protocol_version"] isEqual:@(HelperStatusProtocolVersion)] ||
            ![result[@"state"] isEqual:@"read_only_prototype"] ||
            ![result[@"smc_access"] isEqual:@YES] ||
            ![result[@"write_available"] isEqual:@NO] || result.count != 4) {
            throwFailure(environment, @"XPC status contract mismatch");
            return NULL;
        }
        return encodeResult(environment, result);
    }
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_requestBaselineNative(
    JNIEnv *environment, jobject self
) {
    (void)self;
    @autoreleasepool {
        NSDictionary<NSString *, id> *result = fetchDaemon(environment, DaemonRequestBaseline);
        if (!result) return NULL;
        if ([result[@"available"] isEqual:@NO] &&
            [result[@"reason"] isKindOfClass:NSString.class]) {
            throwFailure(environment, [@"SMC baseline unavailable: " stringByAppendingString:result[@"reason"]]);
            return NULL;
        }
        if (!HelperBaselineResponseValid(result)) {
            throwFailure(environment, @"XPC baseline unavailable or contract mismatch");
            return NULL;
        }
        return encodeResult(environment, result);
    }
}

static jstring requestWatch(JNIEnv *environment, DaemonRequest request) {
    NSDictionary<NSString *, id> *result = fetchDaemon(environment, request);
    if (!result) return NULL;
    if (!HelperWatchResponseValid(result)) {
        throwFailure(environment, @"XPC watch contract mismatch");
        return NULL;
    }
    return encodeResult(environment, result);
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_startWatchNative(
    JNIEnv *environment, jobject self
) {
    (void)self;
    @autoreleasepool { return requestWatch(environment, DaemonRequestWatchStart); }
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_watchStatusNative(
    JNIEnv *environment, jobject self
) {
    (void)self;
    @autoreleasepool { return requestWatch(environment, DaemonRequestWatchStatus); }
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_startupAuditNative(
    JNIEnv *environment, jobject self
) {
    (void)self;
    @autoreleasepool {
        NSDictionary<NSString *, id> *result = fetchDaemon(environment, DaemonRequestStartupAudit);
        if (!result) return NULL;
        if (!HelperStartupAuditResponseValid(result)) {
            throwFailure(environment, @"XPC startup audit contract mismatch");
            return NULL;
        }
        return encodeResult(environment, result);
    }
}

static jstring requestSupervisor(JNIEnv *environment, DaemonRequest request) {
    NSDictionary *result = fetchDaemon(environment, request);
    if (!result) return NULL;
    if (!HelperSupervisorResponseValid(result)) {
        throwFailure(environment, @"XPC supervisor contract mismatch");
        return NULL;
    }
    return encodeResult(environment, result);
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_startSupervisorNative(JNIEnv *environment, jobject self) {
    (void)self;
    @autoreleasepool { return requestSupervisor(environment, DaemonRequestSupervisorStart); }
}
JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_supervisorStatusNative(JNIEnv *environment, jobject self) {
    (void)self;
    @autoreleasepool { return requestSupervisor(environment, DaemonRequestSupervisorStatus); }
}
JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_startSupervisorCrashNative(JNIEnv *environment, jobject self) {
    (void)self;
    @autoreleasepool { return requestSupervisor(environment, DaemonRequestSupervisorCrashStart); }
}
JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_resumeSupervisorNative(JNIEnv *environment, jobject self) {
    (void)self;
    @autoreleasepool { return requestSupervisor(environment, DaemonRequestSupervisorResume); }
}
JNIEXPORT jstring JNICALL Java_ventilator_desktop_helper_HelperProbeNative_cleanupSupervisorNative(JNIEnv *environment, jobject self) {
    (void)self;
    @autoreleasepool { return requestSupervisor(environment, DaemonRequestSupervisorCleanup); }
}
