// The Mac's two Apple-only pieces of gfx, as Linux has them (docs/plans/linux-release.md): no menu
// bar or Dock to hide in fullscreen, and no MetalFX, so a world drawn at `--scale` under 1 reaches
// the screen through the present's own stretch. X11 and Wayland do not hold the thread while a
// window is moved or sized, so there is no held loop to keep ticking either.
#include "gfx/metalfx.h"
#include "gfx/window.h"

#include <string>

namespace mu::gfx {

void Window::setPresentation(bool) {}

void Window::armHeld(bool) {}

// Neither X11 nor Wayland names another program's window to a client, so the log says so.
std::string Window::focusOwner() { return "unknown (Linux)"; }

namespace metalfx {

bool supported() { return false; }
void attach(bgfx::ViewId, bgfx::TextureHandle, bgfx::TextureHandle) {}
void detach() {}

}  // namespace metalfx
}  // namespace mu::gfx
