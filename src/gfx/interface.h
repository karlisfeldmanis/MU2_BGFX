// The windows' drawing: textured quads, polygons and text in backbuffer pixels, in the order
// they were asked for.
//
// Sprint 7's "HUD as one draw and one table", and the overlay's rule kept the other way round:
// the overlay is deliberately NOT this, and this is deliberately not the overlay. The overlay
// is one font on one texture for a bench's list. This carries MU2's interface art -- a plate,
// two gem sheets, a dozen buttons -- and the text over it, so it is one draw per RUN of one
// texture, in submission order, which is what painter's order over several textures costs.
//
// **Retained.** A window keeps a Canvas and rebuilds it only when something it mirrors moved;
// the frame hands every canvas to `submit`, which copies the kept vertices into one transient
// buffer. The costly half of a redraw here is the layout and the strings, not the upload, and
// that half is what "redraw on change" saves. MU2's Hud.Stale is the same argument.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "gfx/face.h"

namespace mu::gfx {

// One piece of art: the handle and its size in texels, which is what a region is cut in.
struct Art {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    float width = 0.0f;
    float height = 0.0f;
    bool valid() const { return bgfx::isValid(handle) && width > 0.0f && height > 0.0f; }
};

struct Box {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float midX() const { return x + w * 0.5f; }
    float midY() const { return y + h * 0.5f; }
    bool has(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Box grown(float by) const { return {x - by, y - by, w + by * 2.0f, h + by * 2.0f}; }
};

enum class Align : uint8_t { Left, Centre, Right };

// ABGR, which is what the vertex carries. Components above 1 are clamped: MU2 brightens a disc
// to 1.35 under the pointer through Godot's modulate, and a byte cannot say that.
constexpr uint32_t rgbaByte(float v) {
    return uint32_t((v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) * 255.0f + 0.5f);
}
constexpr uint32_t rgba(float r, float g, float b, float a = 1.0f) {
    return rgbaByte(r) | (rgbaByte(g) << 8) | (rgbaByte(b) << 16) | (rgbaByte(a) << 24);
}

class Interface;

// One window's list. Filled by the window, kept until the window is stale.
class Canvas {
public:
    void clear();
    bool empty() const { return indices_.empty(); }

    // The whole of a picture, stretched to a box.
    void image(const Art& art, const Box& to, uint32_t abgr = 0xFFFFFFFFu);
    // A region of a picture, in its own texels, stretched to a box. Godot's
    // DrawTextureRectRegion, which is what MU2's tables are written for.
    void region(const Art& art, const Box& to, const Box& from, uint32_t abgr = 0xFFFFFFFFu);
    // A filled rectangle.
    void rect(const Box& box, uint32_t abgr);
    // A filled rectangle with a colour at each corner, top-left round to bottom-left: a hairline
    // that fades out toward its end, and the shadow either side of it.
    void shade(const Box& box, uint32_t topLeft, uint32_t topRight, uint32_t bottomRight,
               uint32_t bottomLeft);
    // A rectangle's outline, `thickness` pixels inside its edge.
    void outline(const Box& box, float thickness, uint32_t abgr);
    // A convex polygon as a fan, with its uvs already normalised. Null art is a solid fill.
    void polygon(const Art* art, const float* xy, const float* uv, int count, uint32_t abgr);

    // A line of text on a baseline, at a Godot font size. `width` is the box a centred or
    // right-aligned line is set in, from `x` -- Godot's DrawString(width:) exactly. Returns
    // how wide the line is.
    float text(float x, float baseline, float fontSize, uint32_t abgr, const std::string& s,
               Align align = Align::Left, float width = 0.0f);
    // The same with a shadow a pixel down and right, which is how MU2 prints over art.
    float shadowed(float x, float baseline, float fontSize, uint32_t abgr, uint32_t shadow,
                   float drop, const std::string& s, Align align = Align::Left,
                   float width = 0.0f);

    // A line in a face of the caller's own on its own texture, `tracking` pixels after every
    // letter -- CSS's letter-spacing. Each quad grows by the face's blur spread, so a blurred
    // bake draws its whole halo. Returns the advance, tracking included.
    float lettered(const Face& face, bgfx::TextureHandle texture, float x, float baseline,
                   float fontSize, float tracking, uint32_t abgr, const std::string& s);

    const Face& face() const;

private:
    friend class Interface;
    struct Vertex {
        float x, y;
        float u, v;
        uint32_t abgr;
    };
    struct Run {
        bgfx::TextureHandle texture;
        uint32_t firstIndex;
        uint32_t count;
    };
    void begin(bgfx::TextureHandle texture);
    void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
              uint32_t abgr);

    const Interface* owner_ = nullptr;
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Run> runs_;
};

// A baked face as a texture: white, its coverage in alpha, and a mip chain. The windows' own
// face goes up this way, and so does any face a window bakes for itself.
bgfx::TextureHandle uploadFace(const Face& face, const char* name);

class Interface {
public:
    bool init(const std::string& shaderDir);
    void shutdown();
    bool ready() const { return bgfx::isValid(program_); }

    // A canvas belongs to this interface's face; a window asks for one here once.
    void adopt(Canvas& canvas) const { canvas.owner_ = this; }

    // Starts a frame: the backbuffer's size, and nothing queued.
    void begin(int width, int height);
    // Queues a canvas for this frame, in the order windows go down.
    void add(const Canvas& canvas);
    // Draws what was queued. One draw call per run, and the count is kept for the log.
    void submit(bgfx::ViewId view);
    uint32_t draws() const { return draws_; }
    uint32_t vertices() const { return vertexCount_; }

    const Face& face() const { return face_; }
    bgfx::TextureHandle faceTexture() const { return faceTexture_; }

private:
    Face face_;
    bgfx::TextureHandle faceTexture_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    std::vector<const Canvas*> queued_;
    int width_ = 0, height_ = 0;
    uint32_t draws_ = 0;
    uint32_t vertexCount_ = 0;
};

}  // namespace mu::gfx
