// What a warp looks like where it lands: MuMain's `CreateEffect(BITMAP_MAGIC + 2, ...)`, which
// ReceiveTeleport throws under the hero on every warp with its Flag set (WSclient.cpp:2220) --
// the Town Portal Scroll's, here. Two things for twenty reference ticks, ZzzEffect.cpp:9932:
//
//   * two walls of `Magic_Circle1.jpg` round him, `RenderCircle(BITMAP_MAGIC + 2, Position, 90,
//     130, 200, ±Rotation, 0, 0)`: twelve quads each, a tile and three tenths across at the
//     foot opening to two and six tenths at the head, two metres tall, one turning each way at
//     a tenth of a degree a millisecond. Lit white at the foot and black at the top
//     (LightTop 0), added over the frame, so each wall fades up into nothing. At full strength
//     for its whole life, as MU draws it: only the circle under them dims;
//   * the ground circle, `RenderTerrainAlphaBitmap(BITMAP_MAGIC + 1, ...)` in MU's orange
//     (1, 0.4, 0.2), opening from nothing to three tiles and dimming a fifth a tick over the
//     last five -- the level-up's circle in another colour.
//
// Hand-lit Gouraud is the one thing the effect pass does not have: a quad takes one colour.
// So each wall quad is cut into kBands up its height, each at the light halfway up its band,
// which is the same fade in steps too small to see. The warp stays where it was thrown, as MU's
// does -- nothing in MoveEffect moves a BITMAP_MAGIC + 2.
//
// And the half that is not here: the hero himself arriving at nought alpha (`o->Alpha = 0.f`)
// is the figure's own fade-in, which the showing already has for a revive (Play::useItem).
#pragma once

#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class Warp {
public:
    // Takes the wall's sheet and the circle's. False when either is missing, and then nothing
    // is thrown: half a warp reads as a bug.
    bool open(const std::string& assetDir, content::Textures& textures);

    // Thrown at `feet`, world metres, at `metresPerTile` to the tile. One at a time: a second
    // before the first is over replaces it, which only a double read of the scroll could ask.
    void land(const float feet[3], float metresPerTile);
    // Ages it on MU's clock. `seconds` is the frame's own.
    void update(float seconds);
    void gather(gfx::Effects& effects, const content::Ground& ground) const;
    bool live() const { return living_; }

    static constexpr int kBands = 4;

private:
    void gatherWall(gfx::Effects& effects, float turn) const;
    void gatherCircle(gfx::Effects& effects, const content::Ground& ground) const;

    bgfx::TextureHandle wall_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle circle_ = BGFX_INVALID_HANDLE;
    bool living_ = false;
    float feet_[3] = {0.0f, 0.0f, 0.0f};
    float per_ = 0.01f;   // metres in one of MU's units
    float age_ = 0.0f;    // reference ticks since it landed
    // The walls' turn is read off MU's WorldTime, a clock that never stops; this is that clock
    // for as long as a warp has been drawn, in seconds.
    double clock_ = 0.0;
};

}  // namespace mu::game
