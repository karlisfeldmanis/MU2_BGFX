// A text overlay, in pixels, for the benches to say what they are showing.
//
// Deliberately small and deliberately NOT the HUD. Sprint 7 builds the HUD as one draw and one
// table with a real text atlas, and this must not become that by accident: it has one font at
// one size, no layout, no input and no windows. What it is for is the viewer's list -- reading
// a model's name off the screen instead of off the log -- and it is priced to the HUD's own
// view, which the present account already covers.
//
// One draw for everything. Every glyph and every panel is a quad in one transient buffer
// against one atlas, so a list of forty names costs the same draw call as one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

namespace mu::gfx {

class Overlay {
public:
    bool init(const std::string& shaderDir);
    void shutdown();

    // Starts a frame's worth of quads. `width` and `height` are the backbuffer's, in pixels.
    void begin(int width, int height);
    // A filled rectangle, in pixels from the top left.
    void panel(float x, float y, float w, float h, uint32_t abgr);
    // One line of text, in pixels from the top left. Returns how wide it was.
    //
    // Every glyph is drawn twice, once a pixel down and right in near-black and once in the
    // colour asked for. A 1px stroke over a photograph of grass is unreadable wherever the
    // grass is pale, and no choice of ink fixes that because the background is both; a shadow
    // does, and costs one more quad in a draw that is already one draw.
    float text(float x, float y, float scale, uint32_t abgr, const std::string& s);
    // What `text` would measure, without drawing it.
    static float measure(float scale, const std::string& s);
    static float lineHeight(float scale);
    void submit(bgfx::ViewId view);

    bool ready() const { return bgfx::isValid(program_); }

private:
    void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
              uint32_t abgr);

    struct Vertex {
        float x, y;
        float u, v;
        uint32_t abgr;
    };
    std::vector<Vertex> vertices_;
    std::vector<uint16_t> indices_;
    bgfx::TextureHandle atlas_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace mu::gfx
