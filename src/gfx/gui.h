// Dear ImGui on bgfx, for the studio's panels and nothing else (docs/order-plan.md §9c, F2).
//
// The user's pick, 2026-10-11: the editor's panels are ImGui, the game's own UI stays as it
// is. So this is drawn only by the studio, into its own view after the HUD (ViewGui), and no
// game frame opens it.
//
// It reads the input the Window already gathered -- the pointer in framebuffer pixels, the
// two buttons, the wheel, what was typed -- rather than GLFW's callbacks, which the Window
// owns. The window's own cursor is blank (window.cpp), so ImGui draws its arrow.
//
// The frame: begin() before the panels, which then call ImGui:: as any ImGui code does, and
// end() after them, which submits. wantsPointer() is the last frame's answer, as ImGui's is:
// the pointer over a panel is the panel's, and the camera does not orbit under it.
#pragma once

#include <string>

#include <bgfx/bgfx.h>

struct ImGuiContext;
struct ImDrawData;

namespace mu::gfx {

class Window;

class Gui {
public:
    // `font` a .ttf, or empty for ImGui's own; `size` in points, scaled by the display's.
    bool open(const Window& window, const std::string& font, float size);
    void close();
    bool isOpen() const { return context_ != nullptr; }

    void begin(const Window& window, float deltaSeconds);
    void end();

    bool wantsPointer() const;
    bool wantsKeys() const;

private:
    void render(ImDrawData* data);

    ImGuiContext* context_ = nullptr;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
};

}  // namespace mu::gfx
