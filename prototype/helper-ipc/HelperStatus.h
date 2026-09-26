#import <Foundation/Foundation.h>

// This protocol deliberately has no SMC key, RPM, file path, or write operation.
@protocol HelperStatusXPC
- (void)fetchStatusWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
@end

static const NSInteger HelperStatusProtocolVersion = 1;
