#import <Foundation/Foundation.h>
#import "SmcBaselineRead.h"

typedef SmcBaselineResult (*StartupAuditReader)(SmcBaselineSnapshot *snapshot);

@interface StartupAuditController : NSObject
+ (instancetype)shared;
- (instancetype)initWithReader:(StartupAuditReader)reader;
- (NSDictionary<NSString *, id> *)status;
@end
