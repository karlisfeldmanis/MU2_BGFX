// The window and bgfx on it. Knows nothing of the game: no map, no figure, no camera.
#pragma once

#include <cstdint>
#include <string>

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

    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLFWwindow* handle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    uint32_t reset_ = 0;
};

}  // namespace mu::gfx
