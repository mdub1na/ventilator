#import "HelperStatus.h"
#import "BaselineWatchController.h"
#import "StartupAuditController.h"
#import "HelperTemperatureValue.h"
#include "SmcBaselineRead.h"
#include <ctype.h>
#include <time.h>

@interface DaemonStatus : NSObject <HelperStatusXPC>
@end

@implementation DaemonStatus
- (void)fetchStatusWithReply:(void (^)(NSDictionary<NSString *, id> *))reply {
    reply(@{
        @"protocol_version": @(HelperStatusProtocolVersion),
        @"state": @"read_only_prototype",
        @"smc_access": @YES,
        @"write_available": @NO
    });
}

- (void)fetchBaselineWithReply:(void (^)(NSDictionary<NSString *, id> *))reply {
    SmcBaselineSnapshot snapshot = {0};
    SmcBaselineResult result = smc_baseline_read(&snapshot);
    if (result != SMC_BASELINE_OK) {
        reply(@{@"protocol_version": @(HelperStatusProtocolVersion),
                @"available": @NO,
                @"reason": [NSString stringWithUTF8String:smc_baseline_result_name(result)]});
        return;
    }
    struct timespec sampled = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &sampled) != 0) {
        reply(@{@"protocol_version": @(HelperStatusProtocolVersion),
                @"available": @NO, @"reason": @"clock_failed"});
        return;
    }
    reply(@{@"protocol_version": @(HelperStatusProtocolVersion),
            @"available": @YES,
            @"model": @"Mac15,7",
            @"macos": @"27.0",
            @"sample_monotonic_ns": @((uint64_t)sampled.tv_sec * 1000000000u +
                                       (uint64_t)sampled.tv_nsec),
            @"baseline": @(smc_baseline_is_system(&snapshot)),
            @"Ftst": @(snapshot.ftst),
            @"mode": @[@(snapshot.mode[0]), @(snapshot.mode[1])],
            @"target_rpm": @[@(snapshot.target_rpm[0]), @(snapshot.target_rpm[1])],
            @"actual_rpm": @[@(snapshot.actual_rpm[0]), @(snapshot.actual_rpm[1])],
            @"temperatures_c": @[HelperTemperatureValue(snapshot.temperatures_c[0]),
                                  HelperTemperatureValue(snapshot.temperatures_c[1]),
                                  HelperTemperatureValue(snapshot.temperatures_c[2])]});
}

- (void)fetchStartupAuditWithReply:(void (^)(NSDictionary<NSString *, id> *))reply {
    reply([[StartupAuditController shared] status]);
}

- (void)startBaselineWatchWithReply:(void (^)(NSDictionary<NSString *, id> *))reply {
    reply([[BaselineWatchController shared] start]);
}

- (void)fetchBaselineWatchWithReply:(void (^)(NSDictionary<NSString *, id> *))reply {
    reply([[BaselineWatchController shared] status]);
}
@end

@interface DaemonListener : NSObject <NSXPCListenerDelegate>
@property(nonatomic, strong) NSMutableArray<NSXPCConnection *> *connections;
@end

@implementation DaemonListener
- (instancetype)init {
    self = [super init];
    if (self) _connections = [NSMutableArray array];
    return self;
}

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    (void)listener;
    NSLog(@"accepted signed XPC connection");
    connection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(HelperStatusXPC)];
    connection.exportedObject = [DaemonStatus new];
    __weak DaemonListener *weakSelf = self;
    __weak NSXPCConnection *weakConnection = connection;
    connection.invalidationHandler = ^{
        NSXPCConnection *invalidated = weakConnection;
        if (invalidated) {
            @synchronized (weakSelf) {
                [weakSelf.connections removeObject:invalidated];
            }
        }
    };
    @synchronized (self) {
        [self.connections addObject:connection];
    }
    [connection resume];
    return YES;
}
@end

static BOOL validTeamID(const char *team) {
    if (strlen(team) != 10) return NO;
    for (size_t index = 0; index < 10; ++index) {
        if (!isalnum((unsigned char)team[index])) return NO;
    }
    return YES;
}

static BOOL validClientIdentifier(const char *identifier) {
    return strcmp(identifier, "com.ventilator.helper-ipc.signed-client") == 0 ||
        strcmp(identifier, "ventilator.desktop") == 0;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc != 4 || !validTeamID(argv[2]) || !validClientIdentifier(argv[3])) {
            fprintf(stderr, "Usage: %s MACH_SERVICE_NAME EXPECTED_CLIENT_TEAM_ID EXPECTED_CLIENT_IDENTIFIER\n", argv[0]);
            return 2;
        }
        NSString *serviceName = [NSString stringWithUTF8String:argv[1]];
        NSString *requirement = [NSString stringWithFormat:
            @"anchor apple generic and identifier \"%s\" and certificate leaf[subject.OU] = \"%s\"",
            argv[3], argv[2]];
        // Capture before the listener accepts the first XPC request.
        [StartupAuditController shared];
        NSXPCListener *listener = [[NSXPCListener alloc] initWithMachServiceName:serviceName];
        DaemonListener *delegate = [DaemonListener new];
        listener.delegate = delegate;
        [listener setConnectionCodeSigningRequirement:requirement];
        [listener resume];
        NSLog(@"read-only signed daemon listener ready");
        [[NSRunLoop currentRunLoop] run];
        [listener invalidate];
        return 0;
    }
}
