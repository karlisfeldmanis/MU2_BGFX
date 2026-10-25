// The Mac's two Apple-only pieces of gfx, as Linux has them (docs/plans/linux-release.md): no menu
// bar or Dock to hide in fullscreen, and no MetalFX, so a world drawn at `--scale` under 1 reaches
// the screen through the present's own stretch. X11 and Wayland do not hold the thread while a
// window is moved or sized, so there is no held loop to keep ticking either.
//
// And a developer's present, MU2_COPY_PRESENT=1: bgfx draws with no window, into a frame of the
// window's size, and each finished picture is read back and put into the X11 window by Xlib. It
// is how the Linux client is watched on this Mac at the GPU's speed -- the M1 through Venus in a
// libkrun machine, whose Vulkan draws but has no swap chain to present with (the plan's "on this
// Mac's GPU"). It tests everything but the swap chain; players never set it.
#include "gfx/metalfx.h"
#include "gfx/views.h"
#include "gfx/window.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/log.h"

// Last: Xlib's macros (None, Bool, Status) would bend any header after them.
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>  // GLFW_EXPOSE_NATIVE_X11 comes from CMake, and Xlib.h with it

namespace mu::gfx {

void Window::setPresentation(bool) {}
float Window::notchPoints() { return 0.0f; }

void Window::armHeld(bool) {}

// Neither X11 nor Wayland names another program's window to a client, so the log says so.
std::string Window::focusOwner() { return "unknown (Linux)"; }

namespace {

// After every view the game has: the copy's blit is the frame's last work.
constexpr bgfx::ViewId kCopyView = 255;

struct Copy {
    bool on = false;
    int width = 0, height = 0;
    bgfx::TextureHandle colour = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle read = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frame = BGFX_INVALID_HANDLE;
    std::vector<uint8_t> pixels;
    bool reading = false;
    uint32_t readyAt = 0;
    // Xlib, opened by hand so the player's build links none of it: GLFW loads it the same way.
    Display* display = nullptr;
    ::Window window = 0;
    GC gc = nullptr;
    XImage* image = nullptr;
    decltype(&XPutImage) put = nullptr;
    decltype(&XFlush) flush = nullptr;
    decltype(&XFreeGC) freeGc = nullptr;
};
Copy g_copy;

template <typename F>
F symbol(void* lib, const char* name) {
    return reinterpret_cast<F>(dlsym(lib, name));
}

}  // namespace

bool copyPresentAsked() {
    const char* asked = std::getenv("MU2_COPY_PRESENT");
    return asked && *asked == '1';
}

void copyPresentOpen(GLFWwindow* window, int width, int height) {
    Copy& c = g_copy;
    void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!x11) {
        core::logError("copy present: no libX11.so.6");
        return;
    }
    auto createGc = symbol<decltype(&XCreateGC)>(x11, "XCreateGC");
    auto createImage = symbol<decltype(&XCreateImage)>(x11, "XCreateImage");
    auto defaultScreen = symbol<decltype(&XDefaultScreen)>(x11, "XDefaultScreen");
    auto defaultVisual = symbol<decltype(&XDefaultVisual)>(x11, "XDefaultVisual");
    auto defaultDepth = symbol<decltype(&XDefaultDepth)>(x11, "XDefaultDepth");
    c.put = symbol<decltype(&XPutImage)>(x11, "XPutImage");
    c.flush = symbol<decltype(&XFlush)>(x11, "XFlush");
    c.freeGc = symbol<decltype(&XFreeGC)>(x11, "XFreeGC");
    if (!createGc || !createImage || !defaultScreen || !defaultVisual || !defaultDepth || !c.put ||
        !c.flush || !c.freeGc) {
        core::logError("copy present: libX11 lacks a function it needs");
        return;
    }

    c.width = width;
    c.height = height;
    // BGRA8, which is the byte order a 24-bit TrueColor X visual reads on a little-endian machine.
    c.colour = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::BGRA8,
                                     BGFX_TEXTURE_RT);
    c.depth = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::D32F,
                                    BGFX_TEXTURE_RT_WRITE_ONLY);
    const bgfx::TextureHandle targets[] = {c.colour, c.depth};
    c.frame = bgfx::createFrameBuffer(2, targets, false);
    c.read = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::BGRA8,
                                   BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    c.pixels.assign(size_t(width) * size_t(height) * 4, 0);
    // Every view starts on the copied frame, as it would on the backbuffer; a view the game points
    // at a target of its own keeps it, and the ones that name the backbuffer ask screenTarget().
    for (int view = 0; view <= kCopyView; ++view) bgfx::setViewFrameBuffer(bgfx::ViewId(view), c.frame);

    c.display = glfwGetX11Display();
    c.window = glfwGetX11Window(window);
    c.gc = createGc(c.display, c.window, 0, nullptr);
    const int screen = defaultScreen(c.display);
    c.image = createImage(c.display, defaultVisual(c.display, screen), unsigned(defaultDepth(c.display, screen)),
                          ZPixmap, 0, reinterpret_cast<char*>(c.pixels.data()), unsigned(width), unsigned(height),
                          32, width * 4);
    c.on = c.gc && c.image;
    core::logf("copy present: %s, %dx%d read back into the X11 window each frame",
               c.on ? "on" : "FAILED", width, height);
}

void copyPresentClose() {
    Copy& c = g_copy;
    if (!c.on) return;
    c.on = false;
    // The image's data is ours (pixels): freed with the vector, never by XDestroyImage.
    if (c.gc) c.freeGc(c.display, c.gc);
    if (bgfx::isValid(c.read)) bgfx::destroy(c.read);
    if (bgfx::isValid(c.frame)) bgfx::destroy(c.frame);
    if (bgfx::isValid(c.colour)) bgfx::destroy(c.colour);
    if (bgfx::isValid(c.depth)) bgfx::destroy(c.depth);
}

bgfx::FrameBufferHandle screenTarget() {
    if (g_copy.on) return g_copy.frame;
    return BGFX_INVALID_HANDLE;
}

void beforeFrame() {
    Copy& c = g_copy;
    if (!c.on || c.reading) return;
    bgfx::touch(kCopyView);
    bgfx::TextureRegion to, from, whole;
    to.init(c.read);
    from.init(c.colour);
    whole.init(c.read);
    bgfx::blit(kCopyView, to, from);
    c.readyAt = bgfx::read(whole, c.pixels.data());
    c.reading = true;
}

void afterFrame(uint32_t frame) {
    Copy& c = g_copy;
    if (!c.on || !c.reading || frame < c.readyAt) return;
    c.put(c.display, c.window, c.gc, c.image, 0, 0, 0, 0, unsigned(c.width), unsigned(c.height));
    c.flush(c.display);
    c.reading = false;
}

// No --offscreen here (gfx/window.cpp's is not built on Linux): a shot goes through bgfx's
// own request, and there is never one to wait for.
bool offscreenShot(const char*) { return false; }
bool offscreenRaw(const char*) { return false; }
void offscreenFlush() {}

namespace metalfx {

bool supported() { return false; }
void attach(bgfx::ViewId, bgfx::TextureHandle, bgfx::TextureHandle) {}
void detach() {}

}  // namespace metalfx
}  // namespace mu::gfx
