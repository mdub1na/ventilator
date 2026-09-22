#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#include <jni.h>
#include <stdatomic.h>

// 0: launch event pending; 1: manual launch; 2: login item launch.
static atomic_int launchMode = 0;
static id launchObserver;

JNIEXPORT void JNICALL Java_ventilator_desktop_login_data_LoginItemNative_installLaunchMonitor(
    JNIEnv *environment, jobject self
) {
    (void)environment;
    (void)self;
    @autoreleasepool {
        if (launchObserver != nil) return;
        launchObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSApplicationDidFinishLaunchingNotification
            object:nil queue:nil usingBlock:^(NSNotification *notification) {
                (void)notification;
                NSAppleEventDescriptor *event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
                BOOL isLoginItem = event.eventID == kAEOpenApplication &&
                    [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue == keyAELaunchedAsLogInItem;
                atomic_store(&launchMode, isLoginItem ? 2 : 1);
            }];
    }
}

JNIEXPORT jint JNICALL Java_ventilator_desktop_login_data_LoginItemNative_launchModeNative(
    JNIEnv *environment, jobject self
) {
    (void)environment;
    (void)self;
    return atomic_load(&launchMode);
}

JNIEXPORT jint JNICALL Java_ventilator_desktop_login_data_LoginItemNative_statusNative(
    JNIEnv *environment, jobject self
) {
    (void)environment;
    (void)self;
    if (@available(macOS 13.0, *)) {
        return (jint)SMAppService.mainAppService.status;
    }
    return -1;
}

JNIEXPORT jstring JNICALL Java_ventilator_desktop_login_data_LoginItemNative_setEnabledNative(
    JNIEnv *environment, jobject self, jboolean enabled
) {
    (void)self;
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService *service = SMAppService.mainAppService;
            SMAppServiceStatus status = service.status;
            if ((enabled && (status == SMAppServiceStatusEnabled || status == SMAppServiceStatusRequiresApproval)) ||
                (!enabled && (status == SMAppServiceStatusNotRegistered || status == SMAppServiceStatusNotFound))) return NULL;

            NSError *error = nil;
            BOOL succeeded = enabled ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
            if (succeeded) return NULL;
            NSString *message = error.localizedDescription ?: @"Service Management returned an unknown error";
            return (*environment)->NewStringUTF(environment, message.UTF8String);
        }
        return (*environment)->NewStringUTF(environment, "macOS 13 or later is required");
    }
}

JNIEXPORT void JNICALL Java_ventilator_desktop_login_data_LoginItemNative_openSettingsNative(
    JNIEnv *environment, jobject self
) {
    (void)environment;
    (void)self;
    if (@available(macOS 13.0, *)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [SMAppService openSystemSettingsLoginItems];
        });
    }
}
