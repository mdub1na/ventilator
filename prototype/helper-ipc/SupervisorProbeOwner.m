#import "SupervisorProbeOwner.h"
#include <fcntl.h>
#include <unistd.h>

@implementation SupervisorProbeOwner {
    BOOL _lost;
    NSFileHandle *_writer;
}
- (BOOL)lost { @synchronized (self) { return _lost; } }
- (NSPipe *)openChannel {
    @synchronized (self) {
        if (_lost || _writer) return nil;
        NSPipe *pipe = [NSPipe pipe];
        int reader = pipe.fileHandleForReading.fileDescriptor;
        int writer = pipe.fileHandleForWriting.fileDescriptor;
        // The grant is queued while BOTH ends are open, before launch. No write
        // occurs after exec, so an exited child cannot send SIGPIPE to daemon.
        if (fcntl(reader, F_SETFD, FD_CLOEXEC) != 0 ||
            fcntl(writer, F_SETFD, FD_CLOEXEC) != 0 || write(writer, "G", 1) != 1) {
            [pipe.fileHandleForReading closeFile];
            [pipe.fileHandleForWriting closeFile];
            return nil;
        }
        _writer = pipe.fileHandleForWriting;
        return pipe;
    }
}
- (void)releaseChannel {
    @synchronized (self) {
        [_writer closeFile];
        _writer = nil;
    }
}
- (void)invalidate {
    @synchronized (self) {
        _lost = YES;
        [self releaseChannel];
    }
}
- (void)attachToConnection:(NSXPCConnection *)connection
            onInvalidation:(dispatch_block_t)cleanup {
    connection.interruptionHandler = ^{ [self invalidate]; };
    connection.invalidationHandler = ^{
        [self invalidate];
        if (cleanup) cleanup();
    };
}
@end
