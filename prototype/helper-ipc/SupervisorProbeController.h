#import <Foundation/Foundation.h>

// Shared by the daemon and its signature-only diagnostic. Never starts a task.
NSURL *SupervisorProbeRunnerURL(void);
BOOL SupervisorProbeCopySignatureCheck(void);

@interface SupervisorProbeController : NSObject
+ (instancetype)shared;
- (NSDictionary<NSString *, id> *)start;
- (NSDictionary<NSString *, id> *)status;
- (NSDictionary<NSString *, id> *)cleanup;
@end
