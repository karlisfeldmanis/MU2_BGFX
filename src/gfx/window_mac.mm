#include "gfx/window.h"

#import <AppKit/AppKit.h>

#include <string>

namespace mu::gfx {

std::string Window::focusOwner() {
    NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    const char* name = front ? [[front localizedName] UTF8String] : nullptr;
    return name ? std::string(name) : std::string("nothing");
}

void Window::setPresentation(bool fullscreen) {
    // HideMenuBar is only accepted beside HideDock.
    const NSApplicationPresentationOptions options =
        fullscreen ? (NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar)
                   : NSApplicationPresentationDefault;
    if ([NSApp presentationOptions] != options) [NSApp setPresentationOptions:options];
}

void Window::armHeld(bool) {}

}  // namespace mu::gfx
