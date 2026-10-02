// Hellfire's ground: the sigil and the wall of fire MU lays at the wizard's feet as he lets the
// spell go. MuMain's MODEL_CIRCLE and MODEL_CIRCLE_LIGHT, subtype 0.
//
// **What MU does, and all of it is kept.** `AT_SKILL_HELL_FIRE` plays PLAYER_SKILL_HELL (0.5; player.muc's
// 154, the leap and the landing) with ten BITMAP_FIREs on his bones a frame while it runs (ZzzCharacter.cpp:5751-5758
// -- here the meteor's burn, as Meteorite's cast has it), and at the let-go makes the two circles
// at his feet with SOUND_HELLFIRE (ZzzCharacter.cpp:4424-4435):
//
//   * **MODEL_CIRCLE** (Circle01.bmd, magic_a01): a flat star four tiles across the radius,
//     LifeTime 45, BlendMesh 0 at `LifeTime * 0.1` -- whole until its last ten frames, then
//     fading (ZzzEffect.cpp:2137-2164, Move_MODEL_CIRCLE at MoveHandlers.cpp:3757-3828).
//   * **MODEL_CIRCLE_LIGHT** (Circle02.bmd, magic_a02): a ring of fire three tiles out and four
//     metres tall, LifeTime 40, rising over its first ten frames and fading as `LifeTime * 0.1`,
//     its sheet scrolled `-LifeTime * 0.01` along U. Every frame it shakes the camera
//     (`EarthQuake = (rand() % 6 - 6) * 0.1`), on one frame in four kicks a MODEL_STONE up
//     somewhere within 300 units, and it lights the ground orange (1, 0.8, 0.2) four tiles round
//     (Move_MODEL_CIRCLE_LIGHT, MoveHandlers.cpp:3830-3870).
//
// **Ours, and marked.** MU's terrain light is a point light here, as Rageful Blow's is
// (fx/fury.h): four tiles, MU's colour, following the wall's brightness.
//
// Presentation only, `game` and not `sim`: the realm struck everything round him on the tick, and
// nothing rolled here reaches the seeded log. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Hellfire {
public:
    // The two circles and their sheets out of effects/hellfire. False when either is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // The circles at `feet`, turned to `yaw`, as the spell is let go.
    void cast(const float feet[3], float yaw);

    // `stone(at)` when the wall kicks up a stone -- the meteor's stones.
    template <typename Stone>
    void update(float seconds, Stone stone);

    void gatherEffects(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    // MU's EarthQuake, in degrees of camera pitch, as fx/meteor.h's.
    float quakeDegrees() const { return quake_; }
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;

    static constexpr float kSigilFrames = 45.0f;  // MODEL_CIRCLE's LifeTime
    static constexpr float kWallFrames = 40.0f;   // MODEL_CIRCLE_LIGHT's
    static constexpr float kWallRise = 10.0f;     // (40 - LifeTime) * 0.1 while over thirty
    static constexpr int kStoneOdds = 4;          // rand_fps_check(4)
    static constexpr float kStoneReach = 300.0f;  // rand() % 300 units
    static constexpr float kGlowTiles = 4.0f;     // AddTerrainLight(..., 4, ...)
    static constexpr float kGlowColour[3] = {1.0f, 0.8f, 0.2f};

    struct Ring {
        bool alive = false;
        float at[3] = {};
        float yaw = 0.0f;   // radians
        float sigil = 0.0f;  // the sigil's LifeTime left, reference frames
        float wall = 0.0f;   // the wall's
    };
    static constexpr int kRings = 6;

    struct Model {
        std::vector<EffectCorner> tris;
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
    };
    Model sigil_, wall_;
    const content::Ground* ground_ = nullptr;
    Ring rings_[kRings] = {};
    float quake_ = 0.0f;
    uint32_t dice_ = 0x4e11f12eu;

    uint32_t roll();
    // The wall's brightness at `left` frames: up over its first ten, then `LifeTime * 0.1`.
    static float wallLight(float left);
};

template <typename Stone>
void Hellfire::update(float seconds, Stone stone) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    bool shaking = false;
    for (Ring& ring : rings_) {
        if (!ring.alive) continue;
        ring.sigil -= frames;
        ring.wall -= frames;
        if (ring.wall > 0.0f) {
            shaking = true;
            // A stone on one frame in four, somewhere within 300 units of the middle.
            if (float(roll() % 10000u) / 10000.0f < frames / float(kStoneOdds)) {
                const float reach = float(roll() % uint32_t(kStoneReach)) * kUnit;
                const float turn = float(roll() % 360u) * kDegrees;
                const float at[3] = {ring.at[0] + std::sin(turn) * reach, ring.at[1],
                                     ring.at[2] + std::cos(turn) * reach};
                stone(at);
            }
        }
        if (ring.sigil <= 0.0f && ring.wall <= 0.0f) ring.alive = false;
    }
    // EarthQuake is set afresh every frame the wall lives, and left where it was after.
    quake_ = shaking ? float(int(roll() % 6u) - 6) * 0.1f : 0.0f;
}

}  // namespace mu::game
