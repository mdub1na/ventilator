#import "HelperStatus.h"
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

static int serve(NSString *serviceName) {
    NSXPCListener *listener = [[NSXPCListener alloc] initWithMachServiceName:serviceName];
    StatusListener *delegate = [StatusListener new];
    listener.delegate = delegate;
    [listener resume];
    NSLog(@"read-only XPC listener ready");
    [[NSRunLoop currentRunLoop] run];
    [listener invalidate];
    return 0;
}

static int request(NSString *serviceName) {
    NSError *error = nil;
    NSXPCConnection *connection = [[NSXPCConnection alloc] initWithMachServiceName:serviceName options:0];
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(HelperStatusXPC)];
    [connection resume];

    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSDictionary<NSString *, id> *result = nil;
    id<HelperStatusXPC> remote = [connection remoteObjectProxyWithErrorHandler:^(NSError *remoteError) {
        fprintf(stderr, "XPC request failed: %s\n", remoteError.localizedDescription.UTF8String);
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

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s serve|request MACH_SERVICE_NAME\n", argv[0]);
            return 2;
        }
        NSString *path = [NSString stringWithUTF8String:argv[2]];
        if (strcmp(argv[1], "serve") == 0) {
            if (geteuid() == 0) {
                fputs("refusing to run an unauthenticated XPC prototype as root\n", stderr);
                return 1;
            }
            return serve(path);
        }
        if (strcmp(argv[1], "request") == 0) return request(path);
        fputs("unknown operation\n", stderr);
        return 2;
    }
}
