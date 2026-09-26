#import <Foundation/Foundation.h>

@interface BaselineWatchController : NSObject
+ (instancetype)shared;
- (NSDictionary<NSString *, id> *)start;
- (NSDictionary<NSString *, id> *)status;
@end
