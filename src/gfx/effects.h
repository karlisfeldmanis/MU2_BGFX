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
//
// Sprint 8b added two that are a blend AND a program: `Flame` is added like `Additive` but read
// through fs_flame's heat ramp, so its colour carries the particle rather than a tint (see that
// shader); `Smoke` is mixed like `Alpha` with a soft disc cut into the sheet.
enum class Blend : uint8_t { Alpha, Additive, Flame, Smoke };

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
    // Which part of the sheet to draw, as a UV rectangle. The whole sheet by default.
    //
    // A rectangle and not a cell index, because MU's sheets are not one shape: blood.tga is
    // 128x128 read as four 64x64 QUADRANTS, the damage digits are ten 16-pixel cells along
    // the top of a 256x32 sheet with the word `Miss` on a second row, and most effects are
    // one picture. An index and a count would serve the strip and need a second scheme for
    // each of the others; a rectangle serves all three and the caller does the arithmetic
    // once, where it knows what the sheet is.
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    Blend blend = Blend::Alpha;
    // A quad that is NOT a billboard: its four corners in world metres and a UV for each,
    // counter-clockwise as the billboard's are (bottom left, bottom right, top right, top
    // left). `position` still has to be set, to the quad's centre, because that is what the
    // depth sort reads. Used for what lies on the ground -- the click marker, cut into a grid
    // that follows the land -- and for a mesh drawn as triangles, which is a quad whose last
    // two corners are the same point. The size, spin and UV rectangle above are ignored.
    bool placed = false;
    float corner[4][3] = {};
    float cornerUv[4][2] = {};
};

class Effects {
public:
    // `capacity` is fixed here and never grown, which is the sprint's proving sentence: the
    // array and the vertex memory are sized once and a frame allocates nothing. A caller
    // that asks for more than this gets refused and counted rather than served, because a
    // pass that quietly reallocates under a busy fight is a hitch in the one moment the
    // whole sprint exists to make look right.
    // 8192 since the level-up: its trails are sampled a quarter tick apart, up to 2280 quads
    // a burst, and two bursts on top of a lit town's 900 did not fit in the old 2048.
    bool init(const std::string& shaderDir, uint32_t capacity = 8192);
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
    // How bright a flame at full heat is, in HDR. fs_flame's u_flame.x; the renderer sets it
    // from the lighting sheet before each draw.
    void setFlameStrength(float strength) { flame_[0] = strength; }

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
    bgfx::ProgramHandle flameProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle smokeProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFlame_ = BGFX_INVALID_HANDLE;
    float flame_[4] = {6.0f, 0.0f, 0.0f, 0.0f};
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
