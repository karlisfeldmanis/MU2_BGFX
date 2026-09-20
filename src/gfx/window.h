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

    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLFWwindow* handle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool clicked_[2] = {false, false};  // 0 left, 1 right
    bool held_[2] = {false, false};
    uint32_t reset_ = 0;
    // Kept whole from init. A resize passes this back with a new size, because a
    // default-constructed SwapChain has a NULL window handle, which bgfx reads as a request
    // for a headless device -- and the window stops presenting.
    bgfx::SwapChain chain_;
};

}  // namespace mu::gfx
