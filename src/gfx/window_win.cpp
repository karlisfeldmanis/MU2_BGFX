// The Mac's two Apple-only pieces of gfx, as Windows has them (docs/plans/windows-client.md):
// no menu bar or Dock to hide in fullscreen, and no MetalFX, so a world drawn at `--scale` under
// 1 reaches the screen through the present's own stretch.
#include "gfx/metalfx.h"
#include "gfx/window.h"

namespace mu::gfx {

void Window::setPresentation(bool) {}

namespace metalfx {

bool supported() { return false; }
void attach(bgfx::ViewId, bgfx::TextureHandle, bgfx::TextureHandle) {}
void detach() {}

}  // namespace metalfx
}  // namespace mu::gfx
