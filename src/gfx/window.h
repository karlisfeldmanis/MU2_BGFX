// The window and bgfx on it. Knows nothing of the game: no map, no figure, no camera.
#pragma once

#include <cstdint>
#include <string>

#include <bgfx/bgfx.h>

struct GLFWwindow;

namespace mu::gfx {

struct WindowDesc {
    int width = 1920;
    int height = 1080;
    bool vsync = false;
    const char* title = "MU2";
};

class Window {
public:
    bool open(const WindowDesc& desc);
    void close();

    // Pumps the OS. False once the window wants to go.
    bool pump();

    bool escapePressed() const;

    // The pointer, in FRAMEBUFFER pixels rather than in the points GLFW reports: on a Retina
    // display the two differ by two, and a pick that unprojects points against a 1920-wide
    // backbuffer lands half way up the screen.
    void pointer(float* x, float* y) const;
    // True once for each press, cleared by the pump that reports it. An edge and not a state,
    // because a click is a thing that happens and a frame at 500 fps sees one press as fifty.
    bool clicked(int button) const { return clicked_[button & 1]; }

    // The same edge rule for the few keys the benches steer by. Named rather than given as
    // GLFW codes so that including this header does not drag GLFW into the game: `gfx` owns
    // the window and nothing above it should know which library opens one.
    //
    // A frame here runs at 400 to 900 fps, so a key held for the shortest press a hand can
    // make is down for several hundred frames. Read as a state, one tap of Right walks the
    // whole model list and lands wherever it ran out.
    enum class Step {
        Previous, Next, PreviousTen, NextTen,
        // Tab walks the viewer's categories -- world objects, monsters, people -- and the two
        // bracket keys walk the clips of whatever figure is standing there.
        Category, PreviousClip, NextClip,
        Count
    };
    bool stepped(Step step) const { return stepped_[size_t(step)]; }

    // Held, rather than the edge `clicked` reports: a drag is a thing that continues.
    bool held(int button) const { return held_[button & 1]; }
    // How far the pointer moved since the last pump, in framebuffer pixels.
    void pointerDelta(float* x, float* y) const { *x = deltaX_; *y = deltaY_; }
    // The wheel since the last pump, in whatever units the OS reports. Positive is towards
    // the screen, which every viewer in the world means as "closer".
    float scroll() const { return scroll_; }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLFWwindow* handle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool clicked_[2] = {false, false};  // 0 left, 1 right
    bool held_[2] = {false, false};
    bool stepped_[size_t(Step::Count)] = {};
    bool stepHeld_[size_t(Step::Count)] = {};
    float lastX_ = 0.0f, lastY_ = 0.0f;
    float deltaX_ = 0.0f, deltaY_ = 0.0f;
    bool hadPointer_ = false;
    float scroll_ = 0.0f;
    uint32_t reset_ = 0;
    // Kept whole from init. A resize passes this back with a new size, because a
    // default-constructed SwapChain has a NULL window handle, which bgfx reads as a request
    // for a headless device -- and the window stops presenting.
    bgfx::SwapChain chain_;
};

}  // namespace mu::gfx
