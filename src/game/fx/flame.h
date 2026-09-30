// The wizard's Flame: MU's `BITMAP_FLAME` at sub-type 0, lit on a tile at the let-go
// (ZzzCharacter.cpp:4480) and burning for forty frames -- a pillar of plumes, a scorch painted on
// the ground under it, stones kicked up, and an orange ground light.
//
// MU2's `client/core/Flame.cs` is the second source, ported: its numbers are MU's, traced there.
// What is MU's (`Move_BITMAP_FLAME`, MoveHandlers.cpp:1794, and the render case beside it):
// every frame of forty, six Flame01 plumes at a fresh +-25 units on the ground plane, each
// climbing 19-38 units a frame straight up for twenty frames at 0.64-1.27 of the sheet, white and
// never fading -- added over each other, they are orange at the edges and white through the core,
// the InfinityMU clip's pillar; a coin in eight for a stone off the meteor's own pile; the sheet
// painted on the land two tiles square, turned by the caster's facing, at a 0.8-1.1 shimmer; and
// (1, 0.4, 0) into the ground over three tiles at the preamble's 0.7-1.0.
//
// Presentation only. The two strikes are the realm's (`Realm::burn`), on the same tile.
#pragma once

#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Meteor;

class Flame {
public:
    // The sheet out of the showing table's "flame", and the ground the scorch is laid on. The
    // stones are the meteor's (`Meteor::stones`); null throws none.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground, Meteor* debris);
    // A fire at `at` (world metres: a tile's centre, on the ground), the scorch turned by the
    // caster's `yaw`.
    void light(const float at[3], float yaw);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

private:
    struct Fire {
        bool alive = false;
        float at[3] = {};
        float yaw = 0.0f;
        float left = 0.0f;   // reference frames
        float owed = 0.0f;   // frames of plumes and stone coins not yet thrown
        float scorch = 1.0f;  // this frame's shimmer on the ground
        float glow = 1.0f;    // and on the light
    };
    struct Plume {
        bool alive = false;
        float at[3] = {};
        float rise = 0.0f;  // metres a second
        float size = 0.5f;  // metres across
        float spin = 0.0f;
        float left = 0.0f;  // reference frames
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kFrames = 40.0f;
    static constexpr int kPlumes = 6;
    static constexpr float kSpread = 25.0f;
    static constexpr uint32_t kStoneOdds = 8;
    static constexpr float kPlumeFrames = 20.0f;
    static constexpr float kSlowest = 19.2f, kFastest = 38.25f;       // units a frame
    static constexpr float kSmallest = 0.64f, kLargest = 1.27f;       // of the sheet
    static constexpr float kSheetUnits = 64.0f;                      // Flame01 is a 64 square
    static constexpr float kScorchTiles = 2.0f;
    static constexpr float kScorchLift = 0.05f;
    static constexpr float kGlow[3] = {1.0f, 0.4f, 0.0f};
    static constexpr float kGlowTiles = 3.0f;

    static constexpr int kFires = 8;
    static constexpr int kMostPlumes = 320;

    const content::Ground* ground_ = nullptr;
    Meteor* debris_ = nullptr;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    Fire fires_[kFires] = {};
    Plume plumes_[kMostPlumes] = {};
    uint32_t dice_ = 0xF1A3E5u;
    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    void layScorch(gfx::Effects& effects, const Fire& fire) const;
};

}  // namespace mu::game
