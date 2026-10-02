#include "gfx/window.h"

#import <AppKit/AppKit.h>

namespace mu::gfx {

void Window::setPresentation(bool fullscreen) {
    // HideMenuBar is only accepted beside HideDock.
    const NSApplicationPresentationOptions options =
        fullscreen ? (NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar)
                   : NSApplicationPresentationDefault;
    if ([NSApp presentationOptions] != options) [NSApp setPresentationOptions:options];
}

}  // namespace mu::gfx
