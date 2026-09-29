#import <Foundation/Foundation.h>

// One object per accepted connection, never a client-supplied identity.
// Invalidation is sticky, including interruption followed by reconnection.
@interface SupervisorProbeOwner : NSObject
@property(nonatomic, readonly) BOOL lost;
- (NSPipe *)openChannel;
- (void)releaseChannel;
- (void)invalidate;
// Shared listener wiring used in the daemon and anonymous XPC test.
- (void)attachToConnection:(NSXPCConnection *)connection
            onInvalidation:(dispatch_block_t)cleanup;
@end
