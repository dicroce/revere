
#include "macos_activation.h"
#import <AppKit/AppKit.h>
#include <SDL.h>
#include <SDL_syswm.h>

void yield_activation_to(const char* bundle_id)
{
    @autoreleasepool
    {
        if(@available(macOS 14.0, *))
            [NSApp yieldActivationToApplicationWithBundleIdentifier:[NSString stringWithUTF8String:bundle_id]];
    }
}

bool macos_window_unseen(SDL_Window* window)
{
    @autoreleasepool
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if(!window || !SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
            return false;
        NSWindow* w = info.info.cocoa.window;
        if(!w)
            return false;
        if(![w isVisible] || [w isMiniaturized])
            return true;
        return ([w occlusionState] & NSWindowOcclusionStateVisible) == 0;
    }
}
