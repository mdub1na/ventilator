#import "SupervisorProbeOwner.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static void readGrant(NSPipe *pipe);

@protocol OwnerPing
- (void)pingWithReply:(void (^)(void))reply;
@end

@interface OwnerPingService : NSObject <OwnerPing>
@end
@implementation OwnerPingService
- (void)pingWithReply:(void (^)(void))reply { reply(); }
@end

@interface OwnerListener : NSObject <NSXPCListenerDelegate>
@property(nonatomic, strong) NSMutableArray<SupervisorProbeOwner *> *owners;
@property(nonatomic, strong) NSMutableArray<NSXPCConnection *> *connections;
@end
@implementation OwnerListener
- (instancetype)init {
    self = [super init];
    if (self) { _owners = [NSMutableArray array]; _connections = [NSMutableArray array]; }
    return self;
}
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    (void)listener;
    SupervisorProbeOwner *owner = [SupervisorProbeOwner new];
    connection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(OwnerPing)];
    connection.exportedObject = [OwnerPingService new];
    [owner attachToConnection:connection onInvalidation:nil];
    @synchronized (self) { [_owners addObject:owner]; [_connections addObject:connection]; }
    [connection resume];
    return YES;
}
@end

static NSXPCConnection *connectPing(NSXPCListener *listener) {
    NSXPCConnection *connection = [[NSXPCConnection alloc] initWithListenerEndpoint:listener.endpoint];
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(OwnerPing)];
    [connection resume];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [[connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error; dispatch_semaphore_signal(done);
    }] pingWithReply:^{ dispatch_semaphore_signal(done); }];
    NSCAssert(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) == 0,
              @"anonymous XPC handshake");
    return connection;
}

static void waitForLoss(SupervisorProbeOwner *owner) {
    uint64_t deadline = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) + UINT64_C(3000000000);
    while (!owner.lost) {
        NSCAssert(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) < deadline,
                  @"XPC invalidation must close ownership promptly");
        struct timespec delay = {.tv_nsec = 10000000}; (void)nanosleep(&delay, NULL);
    }
}

static void xpc_invalidation_only_releases_its_own_connection(void) {
    NSXPCListener *listener = [NSXPCListener anonymousListener];
    OwnerListener *delegate = [OwnerListener new];
    listener.delegate = delegate; [listener resume];
    NSXPCConnection *first = connectPing(listener);
    NSXPCConnection *second = connectPing(listener);
    SupervisorProbeOwner *firstOwner, *secondOwner;
    @synchronized (delegate) {
        NSCAssert(delegate.owners.count == 2, @"two accepted connections");
        firstOwner = delegate.owners[0]; secondOwner = delegate.owners[1];
    }
    NSPipe *channel = [firstOwner openChannel]; readGrant(channel);
    [second invalidate]; waitForLoss(secondOwner);
    char byte;
    NSCAssert(!firstOwner.lost &&
              read(channel.fileHandleForReading.fileDescriptor, &byte, 1) == -1 && errno == EAGAIN,
              @"other connection cannot revoke ownership");
    [first invalidate]; waitForLoss(firstOwner);
    NSCAssert(read(channel.fileHandleForReading.fileDescriptor, &byte, 1) == 0,
              @"real XPC invalidation must cause EOF");
    [channel.fileHandleForReading closeFile];
    [listener invalidate];
}

static void readGrant(NSPipe *pipe) {
    char grant = 0;
    int descriptor = pipe.fileHandleForReading.fileDescriptor;
    NSCAssert(read(descriptor, &grant, 1) == 1 && grant == 'G', @"one queued grant");
    NSCAssert(fcntl(descriptor, F_SETFL, O_NONBLOCK) == 0, @"nonblocking test read");
}

static void other_connection_does_not_release_the_owner(void) {
    SupervisorProbeOwner *owner = [SupervisorProbeOwner new];
    SupervisorProbeOwner *other = [SupervisorProbeOwner new];
    NSPipe *channel = [owner openChannel];
    NSCAssert(channel != nil && [owner openChannel] == nil, @"one active channel per owner");
    NSCAssert((fcntl(channel.fileHandleForWriting.fileDescriptor, F_GETFD) & FD_CLOEXEC) != 0,
              @"writer must not leak through exec");
    readGrant(channel);
    [other invalidate];
    char byte;
    NSCAssert(read(channel.fileHandleForReading.fileDescriptor, &byte, 1) == -1 && errno == EAGAIN,
              @"a different connection cannot cause EOF");
    [owner invalidate];
    NSCAssert(read(channel.fileHandleForReading.fileDescriptor, &byte, 1) == 0,
              @"owner invalidation must close the actual writer even if pipe is retained");
    [channel.fileHandleForReading closeFile];
    [owner invalidate];
    NSCAssert(owner.lost && [owner openChannel] == nil, @"invalidation is sticky and idempotent");
}

static void completed_channel_can_be_reused_but_lost_connection_cannot(void) {
    SupervisorProbeOwner *owner = [SupervisorProbeOwner new];
    NSPipe *first = [owner openChannel];
    readGrant(first);
    [owner releaseChannel];
    char byte;
    NSCAssert(read(first.fileHandleForReading.fileDescriptor, &byte, 1) == 0, @"release closes pipe");
    [first.fileHandleForReading closeFile];
    NSPipe *second = [owner openChannel];
    NSCAssert(second != nil && !owner.lost, @"completed run can open a fresh channel");
    [owner invalidate];
    readGrant(second); // A queued grant alone must not conceal the following EOF.
    NSCAssert(read(second.fileHandleForReading.fileDescriptor, &byte, 1) == 0, @"grant then EOF");
    [second.fileHandleForReading closeFile];
    NSCAssert([owner openChannel] == nil, @"interrupted session cannot regain ownership");
}

static void nstask_receives_only_the_read_end_and_owner_can_close_writer(void) {
    SupervisorProbeOwner *owner = [SupervisorProbeOwner new];
    NSPipe *channel = [owner openChannel];
    NSTask *task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:@"/bin/cat"];
    task.standardInput = channel.fileHandleForReading;
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    NSError *error = nil;
    NSCAssert([task launchAndReturnError:&error], @"real NSTask pipe launch: %@", error);
    [channel.fileHandleForReading closeFile];
    [owner invalidate];
    [task waitUntilExit];
    NSCAssert(task.terminationReason == NSTaskTerminationReasonExit && task.terminationStatus == 0,
              @"child must receive queued grant and EOF");
}

int main(void) {
    @autoreleasepool {
        other_connection_does_not_release_the_owner();
        completed_channel_can_be_reused_but_lost_connection_cannot();
        xpc_invalidation_only_releases_its_own_connection();
        nstask_receives_only_the_read_end_and_owner_can_close_writer();
        puts("supervisor connection owner tests passed");
    }
}
