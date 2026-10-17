
#include "macos_activation.h"
#import <AppKit/AppKit.h>

void yield_activation_to_pid(int64_t pid)
{
    @autoreleasepool
    {
        if(@available(macOS 14.0, *))
        {
            NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t)pid];
            if(app)
                [[NSApplication sharedApplication] yieldActivationToApplication:app];
        }
    }
}
