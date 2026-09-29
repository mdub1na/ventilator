#import <Foundation/Foundation.h>

// The caller cannot choose an SMC key, RPM, file path, or write operation.
@protocol HelperStatusXPC
- (void)fetchStatusWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)fetchBaselineWithReply:(void (^)(NSDictionary<NSString *, id> *snapshot))reply;
- (void)fetchStartupAuditWithReply:(void (^)(NSDictionary<NSString *, id> *audit))reply;
- (void)startBaselineWatchWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)fetchBaselineWatchWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)startSupervisorProbeWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)startSupervisorCrashProbeWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)resumeSupervisorProbeWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)fetchSupervisorProbeWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
- (void)cleanupSupervisorProbeWithReply:(void (^)(NSDictionary<NSString *, id> *status))reply;
@end

static const NSInteger HelperStatusProtocolVersion = 1;
