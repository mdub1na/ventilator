#import <Foundation/Foundation.h>
#import "SupervisorProbeOwner.h"

// Shared by the daemon and its signature-only diagnostic. Never starts a task.
NSURL *SupervisorProbeRunnerURL(void);
BOOL SupervisorProbeCopySignatureCheck(void);

@interface SupervisorProbeController : NSObject
+ (instancetype)shared;
- (NSDictionary<NSString *, id> *)startForOwner:(SupervisorProbeOwner *)owner;
- (NSDictionary<NSString *, id> *)startCrashForOwner:(SupervisorProbeOwner *)owner;
- (NSDictionary<NSString *, id> *)resumeForOwner:(SupervisorProbeOwner *)owner;
- (NSDictionary<NSString *, id> *)status;
- (NSDictionary<NSString *, id> *)cleanupForOwner:(SupervisorProbeOwner *)owner;
@end
