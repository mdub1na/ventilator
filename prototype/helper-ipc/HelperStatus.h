#import <Foundation/Foundation.h>

// The caller cannot choose an SMC key, RPM, file path, or write operation.
@protocol HelperStatusXPC
- (void)fetchStatusWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)fetchBaselineWithReply:(void (^)(NSDictionary<NSString *, id> *snapshot))reply;
@end

static const NSInteger HelperStatusProtocolVersion = 1;
