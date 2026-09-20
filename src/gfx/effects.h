// The transparent pass: MU's effect sprites, sorted and blended into the HDR shade target
// before the tonemap. The first pass in this engine that draws a transparent pixel.
//
// It knows sprites and sheets and nothing about the game -- no blow, no monster, no cue.
// What a blood particle is, how long it lives and what threw it belongs to `game`; this
// takes a list of quads each frame and gets them on the screen in the right order.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

namespace mu::gfx {

// The two MU actually uses, and they are already resolved per part in MU2's index.json, so
// nothing here has to guess one from the art. `Alpha` is a smoke or a splash that covers
// what is behind it; `Additive` is a flame, a spark or a glow, which only ever brightens.
//
// There is deliberately no `Opaque`: an opaque effect is a mesh and belongs in the shade
// pass with the rest of the world's material model, which docs/conventions.md keeps closed.
enum class Blend : uint8_t { Alpha, Additive };

// One quad for one frame. Filled by the caller, read once, and not remembered: the pass has
// no notion of an effect that persists between frames, which is what keeps the lifetime
// rules on the game's side of the layer.
struct Sprite {
    float position[3] = {0.0f, 0.0f, 0.0f};  // world metres, the quad's centre
    float halfWidth = 0.5f;                  // world metres
    float halfHeight = 0.5f;
    // Spin around the axis from the quad to the eye, in radians. MU turns its hit sprites so
    // that four blows on the same spider do not stamp the same picture four times.
    float spin = 0.0f;
    // Multiplied into the sheet. The alpha is the fade, and it multiplies the sheet's own --
    // so a sprite with no alpha in its art still fades out, which is how MoveEffect's
    // preamble shimmer works.
    float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
    // Which cell of a strip sheet to draw, and how many cells the strip has. `cells` of 1 is
    // the whole sheet and is the common case. MU's animated sheets are a horizontal strip.
    uint8_t cell = 0;
    uint8_t cells = 1;
    Blend blend = Blend::Alpha;
};

class Effects {
public:
    // `capacity` is fixed here and never grown, which is the sprint's proving sentence: the
    // array and the vertex memory are sized once and a frame allocates nothing. A caller
    // that asks for more than this gets refused and counted rather than served, because a
    // pass that quietly reallocates under a busy fight is a hitch in the one moment the
    // whole sprint exists to make look right.
    bool init(const std::string& shaderDir, uint32_t capacity = 2048);
    void shutdown();

    // Empties the list. Called once at the top of a frame, before anything adds.
    void begin();
    // Returns false when the pass is full, having counted it. A refused sprite is a sprite
    // nobody sees; it is never a reason to stop drawing the rest.
    bool add(const Sprite& sprite);

    // Sorts back to front and submits. `view` and `proj` are the camera's own, the same two
    // matrices the shade pass used -- passed in rather than recomputed, because a second
    // copy of those calls is how a pass starts disagreeing with the picture it draws into.
    // `eye` is the camera position, which is what the depth sort and the billboard are built
    // from. Draws nothing and touches the view when the list is empty.
    void draw(uint16_t view, const float* viewMtx, const float* projMtx, const float* eye);

    uint32_t lastDrawCount() const { return drawCount_; }
    uint32_t lastSpriteCount() const { return lastSprites_; }
    // The most sprites ever live at once, and how many were refused over the whole run. Both
    // go in the log every second: "no allocation in the pools" is a claim, and a high-water
    // mark well under the capacity is the evidence for it. A refusal count above zero means
    // the capacity is wrong and the picture is already missing something.
    uint32_t highWater() const { return highWater_; }
    uint32_t refused() const { return refused_; }
    uint32_t capacity() const { return uint32_t(sprites_.capacity()); }

private:
    // One corner of one quad. Position is already in world space, billboarded on the CPU --
    // four vertices a sprite against a vertex shader that would have to be handed the
    // camera's basis and rebuild them per vertex. At a few hundred sprites the CPU cost is
    // nothing and it keeps the shader to a transform and a fetch.
    struct Vertex {
        float x, y, z;
        float u, v;
        uint32_t abgr;
    };

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sSheet_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;

    std::vector<Sprite> sprites_;
    // Indices into `sprites_`, sorted by distance from the eye. Sorting the indices and not
    // the sprites moves 4 bytes instead of 64.
    std::vector<uint32_t> order_;
    // Reserved to capacity at init and reused. `bgfx::allocTransientVertexBuffer` owns the
    // per-frame vertex memory, which is bgfx's own ring and not an allocation of ours.
    uint32_t drawCount_ = 0;
    uint32_t lastSprites_ = 0;
    uint32_t highWater_ = 0;
    uint32_t refused_ = 0;
};

}  // namespace mu::gfx
