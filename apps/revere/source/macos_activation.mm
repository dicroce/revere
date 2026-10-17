
#include "macos_activation.h"
#import <AppKit/AppKit.h>

void yield_activation_to(const char* bundle_id)
{
    @autoreleasepool
    {
        if(@available(macOS 14.0, *))
            [NSApp yieldActivationToApplicationWithBundleIdentifier:[NSString stringWithUTF8String:bundle_id]];
    }
}
