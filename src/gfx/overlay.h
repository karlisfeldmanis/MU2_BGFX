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

#include "gfx/face.h"

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
    //
    // A member and no longer static, because the face is proportional: an I and a W are not
    // the same width in Open Sans, and a list panel sized by `count * oneWidth` is either
    // too narrow for its longest name or half empty. Every caller that lays a panel out
    // around a string has to ask the face that will draw it.
    float measure(float scale, const std::string& s) const;
    // The line box, and it is deliberately the SAME arithmetic the bitmap face used --
    // eight pixels a unit of scale. The layout in the viewer is written against it, and a
    // face is a face rather than a new set of margins.
    static float lineHeight(float scale) { return 8.0f * scale; }
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
    // How much a baked glyph is shrunk to meet `lineHeight(scale)`.
    float faceScale(float scale) const;
    // What the face is rasterised at. One size, scaled down to whatever a caller asks for:
    // the overlay's own list draws at a 16 px line, so 40 leaves every label a minification
    // of a well-formed glyph rather than a magnification of a small one.
    static constexpr float kBakePixels = 40.0f;
    Face face_;
    bool haveFace_ = false;
    // Where the solid texel for a panel sits, in uv. It moves with the face: the bitmap
    // fallback keeps it in cell 0 and the baked face puts it below the packed rows.
    float solidU_ = 0.0f, solidV_ = 0.0f;

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
