#include "gfx/window.h"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>  // GLFW_EXPOSE_NATIVE_COCOA comes from CMake

#include <bgfx/bgfx.h>

#include "core/log.h"
#include "gfx/callback.h"
#include "gfx/views.h"

namespace mu::gfx {
namespace {

// GLFW reports the wheel through a callback and nothing else, so it is accumulated here and
// drained by the pump. One window, so one accumulator: a second would need the user pointer
// and there is not going to be a second.
double s_scroll = 0.0;

void onScroll(GLFWwindow*, double, double y) { s_scroll += y; }

// And the typing, the same way: the characters and the three editing keys, drained by the pump.
std::string s_typed;
int s_backspaces = 0;
bool s_entered = false, s_escaped = false;

void onChar(GLFWwindow*, unsigned int code) {
    // As UTF-8, so a box that takes a word later has nothing to change here.
    if (code < 0x80) {
        s_typed += char(code);
    } else if (code < 0x800) {
        s_typed += char(0xC0 | (code >> 6));
        s_typed += char(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        s_typed += char(0xE0 | (code >> 12));
        s_typed += char(0x80 | ((code >> 6) & 0x3F));
        s_typed += char(0x80 | (code & 0x3F));
    }
}

void onKey(GLFWwindow*, int key, int, int action, int) {
    if (action == GLFW_RELEASE) return;
    if (key == GLFW_KEY_BACKSPACE) ++s_backspaces;  // press and repeat
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) s_entered = true;
    if (key == GLFW_KEY_ESCAPE) s_escaped = true;
}

Callback g_callback;

void onGlfwError(int code, const char* what) { core::logError("glfw %d: %s", code, what); }

}  // namespace

bool Window::open(const WindowDesc& desc) {
    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) {
        core::logError("glfw did not start");
        return false;
    }
    // bgfx makes the Metal layer; GLFW must not make a GL context beside it.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Fullscreen takes the display at the mode it is already in -- no mode switch, so no black
    // flash and no display left in another resolution if the run dies. On this Mac that is
    // 2560x1440. The size asked for on the command line is not used then: the display's.
    GLFWmonitor* monitor = nullptr;
    int width = desc.width, height = desc.height;
    if (desc.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        if (const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr) {
            width = mode->width;
            height = mode->height;
            glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        } else {
            core::logError("no display to go fullscreen on; staying in a window");
            monitor = nullptr;
        }
    }
    handle_ = glfwCreateWindow(width, height, desc.title, monitor, nullptr);
    if (!handle_) {
        core::logError("no window");
        return false;
    }

    glfwSetScrollCallback(handle_, onScroll);
    glfwSetCharCallback(handle_, onChar);
    glfwSetKeyCallback(handle_, onKey);

    // MU's own pointer is drawn into the picture (game/cursor.h), and the real one hidden
    // underneath: two of them a few pixels apart is worse than either. MU2's own remark on
    // Pointer.cs applies unchanged -- this is what a screenshot shows, so the desktop's own
    // cursor must not be in it too.
    glfwSetInputMode(handle_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    // The backbuffer is asked for in pixels, not in points: on a Retina display the two
    // differ by two, and a frame measured at the wrong size is not the frame.
    glfwGetFramebufferSize(handle_, &width_, &height_);

    bgfx::Init init;
    init.type = bgfx::RendererType::Metal;  // this Mac only; see docs/conventions.md
    // The window goes on the swap chain, not on platformData: this bgfx asks for it there,
    // and it is the swap chain that carries the size, the formats and the latency. Kept in
    // chain_ so a resize can hand the same description back with only the size changed.
    //
    // Taken FROM init, not written over it. A bare SwapChain leaves formatColor as
    // TextureFormat::Count, while Init's own constructor fills in BGRA8, D24S8 and two back
    // buffers; handing the bare one to bgfx makes CAMetalLayer throw on setPixelFormat
    // before the first frame.
    chain_ = init.swapChain;
    chain_.nwh = glfwGetCocoaWindow(handle_);
    chain_.width = uint32_t(width_);
    chain_.height = uint32_t(height_);
    init.swapChain = chain_;
    // `profile` is what fills bgfx::Stats::viewStats, which is where every account's time
    // comes from. It stays on: a run that cannot be priced is a run wasted.
    init.profile = true;
    reset_ = desc.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    init.reset = reset_;
    init.callback = &g_callback;
    // Before init, and it is what keeps rendering on this thread in a multithreaded build: bgfx
    // starts no render thread when this has already been called. CMakeLists.txt says why the
    // build is multithreaded at all.
    //
    // Priced on 2026-09-24 with the call taken out, so bgfx ran its own render thread: the
    // same at 2560x1273 (6.34 and 6.31 ms against 6.31 and 6.93, interleaved) and 0.08 ms
    // faster at half scale. The frame is the GPU's, not the encoder's, and a render thread
    // costs a frame of latency, so it stays on this thread. docs/budget.md.
    bgfx::renderFrame();
    if (!bgfx::init(init)) {
        core::logError("bgfx did not start");
        return false;
    }

    // Per-view GPU times are what the budget accounts are made of, and on Metal bgfx only
    // takes them when this debug flag is set — `Init::profile` alone leaves viewStats empty
    // and every account reads 0.000, which looks like a frame that costs nothing.
    //
    // Only when asked for. With the flag set, bgfx's Metal backend ends the render pass at
    // EVERY view (renderer_mtl.cpp, `|| profileViews`) so that it can time each one, where it
    // otherwise keeps one pass per target: views 4 and 5 share the shade target, and split,
    // the 4x MSAA colour and the depth are stored after the shade and loaded again for the
    // sprites. Found in a Metal System Trace, where the transparent pass was 774 us of a
    // 6.3 ms frame; switched off, the frame measured 0.77 ms faster at 2560x1273 and drew the
    // same picture. core::Args::views.
    if (desc.profileViews) bgfx::setDebug(BGFX_DEBUG_PROFILER);

    const bgfx::Caps* caps = bgfx::getCaps();
    core::logf("%s, %dx%d, vsync %s", bgfx::getRendererName(caps->rendererType), width_, height_,
               desc.vsync ? "on" : "off");
    // Logged because docs/conventions.md refuses to assume any of them.
    core::logf("homogeneous depth %d, origin bottom left %d, max texture %u, 32-bit indices %d",
               caps->homogeneousDepth, caps->originBottomLeft, caps->limits.maxTextureSize,
               (caps->supported & BGFX_CAPS_INDEX32) != 0);
    return true;
}

void Window::close() {
    bgfx::shutdown();
    if (handle_) glfwDestroyWindow(handle_);
    glfwTerminate();
    handle_ = nullptr;
}

bool Window::pump() {
    glfwPollEvents();
    if (glfwWindowShouldClose(handle_)) return false;

    // Edges, taken here and cleared here: polled state would report one press for every frame
    // it lasts, which at 500 fps is fifty walk orders for one click.
    const int buttons[2] = {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT};
    for (int i = 0; i < 2; ++i) {
        const bool down = glfwGetMouseButton(handle_, buttons[i]) == GLFW_PRESS;
        clicked_[i] = down && !held_[i];
        released_[i] = !down && held_[i];
        held_[i] = down;
    }

    // The game's keys. Two for the bag, because MU binds both I and V to it.
    const int keys[size_t(Key::Count)][2] = {{GLFW_KEY_I, GLFW_KEY_V}, {GLFW_KEY_C, -1},
                                             {GLFW_KEY_1, -1},         {GLFW_KEY_2, -1},
                                             {GLFW_KEY_3, -1},         {GLFW_KEY_4, -1},
                                             {GLFW_KEY_5, -1},         {GLFW_KEY_Q, -1},
                                             {GLFW_KEY_W, -1},         {GLFW_KEY_E, -1},
                                             {GLFW_KEY_R, -1},         {GLFW_KEY_T, -1},
                                             {GLFW_KEY_L, -1}};
    shift_ = glfwGetKey(handle_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
             glfwGetKey(handle_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    for (size_t i = 0; i < size_t(Key::Count); ++i) {
        bool down = false;
        for (int k : keys[i]) {
            if (k >= 0 && glfwGetKey(handle_, k) == GLFW_PRESS) down = true;
        }
        keyPressed_[i] = down && !keyHeld_[i];
        keyHeld_[i] = down;
    }

    // The step keys, on the same edge rule as the buttons. Left and Right walk one, Down and
    // Up walk ten, which is what makes a list of five hundred models usable by hand.
    const int stepKeys[size_t(Step::Count)] = {GLFW_KEY_LEFT,         GLFW_KEY_RIGHT,
                                               GLFW_KEY_DOWN,         GLFW_KEY_UP,
                                               GLFW_KEY_TAB,          GLFW_KEY_LEFT_BRACKET,
                                               GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_T};
    for (size_t i = 0; i < size_t(Step::Count); ++i) {
        const bool down = glfwGetKey(handle_, stepKeys[i]) == GLFW_PRESS;
        stepped_[i] = down && !stepHeld_[i];
        stepHeld_[i] = down;
    }

    // The pointer's movement since the last pump, and the wheel's. The first pump after the
    // window opens has no previous position to subtract, and using (0,0) as one throws the
    // camera across the room on the first frame a button is down.
    float px = 0.0f, py = 0.0f;
    pointer(&px, &py);
    deltaX_ = hadPointer_ ? px - lastX_ : 0.0f;
    deltaY_ = hadPointer_ ? py - lastY_ : 0.0f;
    lastX_ = px;
    lastY_ = py;
    hadPointer_ = true;
    scroll_ = float(s_scroll);
    s_scroll = 0.0;
    typed_.swap(s_typed);
    s_typed.clear();
    backspaces_ = s_backspaces;
    s_backspaces = 0;
    entered_ = s_entered;
    escaped_ = s_escaped;
    s_entered = s_escaped = false;
    // An Escape that lands while a box is open belongs to the box, until the key is let go.
    const bool escapeDown = glfwGetKey(handle_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (typing_ && (escaped_ || escapeDown)) escapeSwallowed_ = true;
    if (!escapeDown) escapeSwallowed_ = false;

    int w = 0, h = 0;
    glfwGetFramebufferSize(handle_, &w, &h);
    if (w != width_ || h != height_) {
        width_ = w;
        height_ = h;
        chain_.width = uint32_t(w);
        chain_.height = uint32_t(h);
        bgfx::reset(reset_, &chain_);
        core::logf("resized to %dx%d", w, h);
    }
    return true;
}

bool Window::fullscreen() const { return handle_ && glfwGetWindowMonitor(handle_) != nullptr; }

void Window::setFullscreen(bool on) {
    if (!handle_ || on == fullscreen()) return;
    if (on) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) {
            core::logError("no display to go fullscreen on; staying in a window");
            return;
        }
        glfwGetWindowPos(handle_, &windowedX_, &windowedY_);
        glfwGetWindowSize(handle_, &windowedW_, &windowedH_);
        glfwSetWindowMonitor(handle_, monitor, 0, 0, mode->width, mode->height,
                             mode->refreshRate);
    } else {
        glfwSetWindowMonitor(handle_, nullptr, windowedX_, windowedY_, windowedW_, windowedH_,
                             GLFW_DONT_CARE);
    }
    core::logf("window: %s", on ? "fullscreen" : "windowed");
}

void Window::windowSize(int* width, int* height) const {
    *width = *height = 0;
    if (handle_) glfwGetWindowSize(handle_, width, height);
}

void Window::setWindowSize(int width, int height) {
    if (!handle_ || width <= 0 || height <= 0) return;
    if (fullscreen()) {
        // Taken as the size to come back to: fullscreen keeps the display's own.
        windowedW_ = width;
        windowedH_ = height;
        return;
    }
    glfwSetWindowSize(handle_, width, height);
    core::logf("window: sized to %dx%d points", width, height);
}

void Window::displaySize(int* width, int* height) const {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    *width = mode ? mode->width : 0;
    *height = mode ? mode->height : 0;
}

void Window::setVsync(bool on) {
    reset_ = on ? (reset_ | BGFX_RESET_VSYNC) : (reset_ & ~uint32_t(BGFX_RESET_VSYNC));
    bgfx::reset(reset_, &chain_);
    core::logf("window: vsync %s", on ? "on" : "off");
}

void Window::holdVsync(bool off) {
    bgfx::reset(off ? (reset_ & ~uint32_t(BGFX_RESET_VSYNC)) : reset_, &chain_);
}

int Window::refreshHz() const {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    return (mode && mode->refreshRate > 0) ? mode->refreshRate : 60;
}

void Window::pointer(float* x, float* y) const {
    double px = 0.0, py = 0.0;
    glfwGetCursorPos(handle_, &px, &py);
    // Points to pixels, by the ratio the framebuffer actually has. Asking GLFW for the content
    // scale is the other way to get it and is a different number on a window straddling two
    // displays; this one is the ratio of the two sizes we already hold.
    int windowWidth = 0, windowHeight = 0;
    glfwGetWindowSize(handle_, &windowWidth, &windowHeight);
    *x = float(px) * (windowWidth > 0 ? float(width_) / float(windowWidth) : 1.0f);
    *y = float(py) * (windowHeight > 0 ? float(height_) / float(windowHeight) : 1.0f);
}

bool Window::escapePressed() const {
    return !escapeHeld_ && !typing_ && !escapeSwallowed_ && glfwGetKey(handle_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
}

}  // namespace mu::gfx
