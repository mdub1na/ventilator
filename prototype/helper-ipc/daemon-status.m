#import "HelperStatus.h"
#include <ctype.h>

static NSString *const ClientIdentifier = @"com.ventilator.helper-ipc.signed-client";

@interface DaemonStatus : NSObject <HelperStatusXPC>
@end

@implementation DaemonStatus
- (void)fetchStatusWithReply:(void (^)(NSDictionary<NSString *, id> *))reply {
    reply(@{
        @"protocol_version": @(HelperStatusProtocolVersion),
        @"state": @"read_only_prototype",
        @"smc_access": @NO,
        @"write_available": @NO
    });
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

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc != 3 || !validTeamID(argv[2])) {
            fprintf(stderr, "Usage: %s MACH_SERVICE_NAME EXPECTED_CLIENT_TEAM_ID\n", argv[0]);
            return 2;
        }
        NSString *serviceName = [NSString stringWithUTF8String:argv[1]];
        NSString *requirement = [NSString stringWithFormat:
            @"anchor apple generic and identifier \"%@\" and certificate leaf[subject.OU] = \"%s\"",
            ClientIdentifier, argv[2]];
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
