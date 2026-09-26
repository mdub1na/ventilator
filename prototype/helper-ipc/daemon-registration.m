#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

static NSString *statusName(SMAppServiceStatus status) {
    switch (status) {
        case SMAppServiceStatusNotRegistered: return @"notRegistered";
        case SMAppServiceStatusEnabled: return @"enabled";
        case SMAppServiceStatusRequiresApproval: return @"requiresApproval";
        case SMAppServiceStatusNotFound: return @"notFound";
    }
    return @"unknown";
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc != 2) {
            fprintf(stderr, "Usage: %s status|register|unregister\n", argv[0]);
            return 2;
        }
        SMAppService *service = [SMAppService daemonServiceWithPlistName:
            @"com.ventilator.helper-ipc.signed-daemon-test.plist"];
        if (strcmp(argv[1], "status") == 0) {
            puts(statusName(service.status).UTF8String);
            return 0;
        }
        NSError *error = nil;
        BOOL ok;
        if (strcmp(argv[1], "register") == 0) {
            ok = [service registerAndReturnError:&error];
        } else if (strcmp(argv[1], "unregister") == 0) {
            ok = [service unregisterAndReturnError:&error];
        } else {
            fputs("unknown operation\n", stderr);
            return 2;
        }
        if (!ok) {
            fprintf(stderr, "%s failed: %s\n", argv[1], error.localizedDescription.UTF8String);
            return 1;
        }
        puts(statusName(service.status).UTF8String);
        return 0;
    }
}
