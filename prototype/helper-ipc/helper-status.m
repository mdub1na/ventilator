#import "HelperStatus.h"
#include <ctype.h>
#include <unistd.h>

@interface StatusProvider : NSObject <HelperStatusXPC>
@end

@implementation StatusProvider
- (void)fetchStatusWithReply:(void (^)(NSDictionary<NSString *, id> *))reply {
    reply(@{
        @"protocol_version": @(HelperStatusProtocolVersion),
        @"state": @"read_only_prototype",
        @"smc_access": @NO,
        @"write_available": @NO
    });
}
@end

@interface StatusListener : NSObject <NSXPCListenerDelegate>
@property(nonatomic, strong) NSMutableArray<NSXPCConnection *> *connections;
@end

@implementation StatusListener
- (instancetype)init {
    self = [super init];
    if (self) _connections = [NSMutableArray array];
    return self;
}

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    (void)listener;
    NSLog(@"accepted XPC connection");
    connection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(HelperStatusXPC)];
    connection.exportedObject = [StatusProvider new];
    __weak StatusListener *weakSelf = self;
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

static int serve(NSString *serviceName, NSString *clientRequirement) {
    NSXPCListener *listener = [[NSXPCListener alloc] initWithMachServiceName:serviceName];
    StatusListener *delegate = [StatusListener new];
    listener.delegate = delegate;
    [listener setConnectionCodeSigningRequirement:clientRequirement];
    [listener resume];
    NSLog(@"read-only XPC listener ready");
    [[NSRunLoop currentRunLoop] run];
    [listener invalidate];
    return 0;
}

static int request(NSString *serviceName, NSString *serverRequirement) {
    NSError *error = nil;
    NSXPCConnection *connection = [[NSXPCConnection alloc] initWithMachServiceName:serviceName options:0];
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(HelperStatusXPC)];
    [connection setCodeSigningRequirement:serverRequirement];
    [connection resume];

    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSDictionary<NSString *, id> *result = nil;
    __block NSError *requestError = nil;
    id<HelperStatusXPC> remote = [connection remoteObjectProxyWithErrorHandler:^(NSError *remoteError) {
        requestError = remoteError;
        fprintf(stderr, "XPC request failed: domain=%s code=%ld detail=%s\n",
                remoteError.domain.UTF8String, (long)remoteError.code,
                remoteError.localizedDescription.UTF8String);
        dispatch_semaphore_signal(done);
    }];
    [remote fetchStatusWithReply:^(NSDictionary<NSString *, id> *status) {
        result = status;
        dispatch_semaphore_signal(done);
    }];
    long waitResult = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    [connection invalidate];
    if (waitResult != 0) {
        fputs("XPC request timed out\n", stderr);
        return 1;
    }
    if (requestError) return 1;
    if (![result[@"protocol_version"] isEqual:@(HelperStatusProtocolVersion)] ||
        ![result[@"state"] isEqual:@"read_only_prototype"] ||
        ![result[@"smc_access"] isEqual:@NO] ||
        ![result[@"write_available"] isEqual:@NO] || result.count != 4) {
        fputs("XPC status contract mismatch\n", stderr);
        return 1;
    }
    NSData *json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:&error];
    if (!json) {
        fprintf(stderr, "status encoding failed: %s\n", error.localizedDescription.UTF8String);
        return 1;
    }
    fwrite(json.bytes, 1, json.length, stdout);
    fputc('\n', stdout);
    return 0;
}

static BOOL validCDHash(const char *hash) {
    if (strlen(hash) != 40) return NO;
    for (size_t index = 0; index < 40; ++index) {
        if (!isxdigit((unsigned char)hash[index])) return NO;
    }
    return YES;
}

static BOOL validTeamID(const char *team) {
    if (strlen(team) != 10) return NO;
    for (size_t index = 0; index < 10; ++index) {
        if (!isalnum((unsigned char)team[index])) return NO;
    }
    return YES;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc == 4 && strcmp(argv[1], "request-signed") == 0 && validTeamID(argv[3])) {
            NSString *requirement = [NSString stringWithFormat:
                @"anchor apple generic and identifier \"com.ventilator.helper-ipc.signed-daemon\" and certificate leaf[subject.OU] = \"%s\"",
                argv[3]];
            return request([NSString stringWithUTF8String:argv[2]], requirement);
        }
        if (argc != 4 || !validCDHash(argv[3])) {
            fprintf(stderr, "Usage: %s serve|request MACH_SERVICE_NAME EXPECTED_PEER_CDHASH\n"
                    "   or: %s request-signed MACH_SERVICE_NAME EXPECTED_DAEMON_TEAM_ID\n", argv[0], argv[0]);
            return 2;
        }
        NSString *serviceName = [NSString stringWithUTF8String:argv[2]];
        NSString *peerRequirement = [NSString stringWithFormat:@"cdhash H\"%s\"", argv[3]];
        if (strcmp(argv[1], "serve") == 0) {
            if (geteuid() == 0) {
                fputs("refusing to run an ad hoc-only XPC prototype as root\n", stderr);
                return 1;
            }
            return serve(serviceName, peerRequirement);
        }
        if (strcmp(argv[1], "request") == 0) return request(serviceName, peerRequirement);
        fputs("unknown operation\n", stderr);
        return 2;
    }
}
