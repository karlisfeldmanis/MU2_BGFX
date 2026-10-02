// Twister's storm: the whirlwind MU stands at the wizard's feet as he lets the spell go and walks
// out along where he faced. MuMain's MODEL_STORM, sub-type 0.
//
// **What MU does, and all of it is kept but the ground's dark.** `AT_SKILL_STORM` plays
// SetPlayerMagic's two hands and at the let-go makes the storm at his position and angle with
// SOUND_STORM (ZzzCharacter.cpp:4495-4497). Its create arm (ZzzEffect.cpp:2000-2010) gives it
// LifeTime 59, BlendMesh 0 and `Direction = (0, -10, 0)`, which the shared tail walks along its
// angle, ten units a frame; its scale is CreateEffect's 0.9. Every frame of its life
// (Move_MODEL_STORM, MoveHandlers.cpp:3384-3427):
//
//   * the funnel (Storm01, typhoon) added at `BlendMeshLight = LifeTime * 0.1`, which glColor
//     clamps at one, its sheet scrolled `BlendMeshTexCoordU = -LifeTime * 0.1` along U, and its
//     one action stepped at 0.3 keys a frame -- nineteen of its thirty keys reached, each blended
//     with the next as BMD::Animation blends them (source/effects/storm/Storm01.json);
//   * a BITMAP_SMOKE sub-type 3 puff at its foot: LifeTime 10, scale 0.8 to 1.11, thrown 40-47
//     units a frame at a pitch of -45 to 45 and a random yaw, its speed 0.4 of itself a frame,
//     growing a tenth a frame, lit (0.8, 0.8, 1) at `LifeTime / 8`;
//   * two coins of one in two, each a thin BITMAP_JOINT_THUNDER from two metres east or west of
//     it and seven up, down to it (fx/thunder.h `fork`);
//   * one frame in four, a MODEL_STONE at its foot -- the meteor's stones;
//   * its foot snapped to the ground.
//
// **Not ported**: `AddTerrainLight` with (-0.4, -0.3, -0.2) over five tiles, a light taken OUT of
// the ground -- this renderer only adds, as fx/ice.h says of the ice's.
//
// **Ours, and marked**: in its place a faint cool light on the ground under it, the smoke's own
// (0.8, 0.8, 1) tint, three tiles round, fading with the funnel (the user, 2026-10-02: "add some
// minimal light emiter to twister").
//
// Presentation only, `game` and not `sim`: the realm walks its own storm down the same line and
// strikes on its own ticks (`Realm::burn`, `SkillRow::walks`). Pools are sized once and a frame
// allocates nothing.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Storm {
public:
    // The nineteen poses and typhoon out of effects/storm, smoke01 off the table. False when the
    // poses or the sheet are missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A storm at `feet`, walking along the flat world direction (`wayX`, `wayZ`), unit length.
    void cast(const float feet[3], float wayX, float wayZ);

    // `fork(from, to)` for a thin bolt down into it, `stone(at)` when it kicks one up.
    template <typename Fork, typename Stone>
    void update(float seconds, Fork fork, Stone stone);

    void gatherEffects(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kTwoPi = 6.28318531f;

    static constexpr int kKeys = 19;                // Storm01_00..18, all that 59 frames reach
    static constexpr float kKeysPerFrame = 0.3f;    // CreateEffect's Velocity
    static constexpr float kFrames = 59.0f;         // LifeTime
    static constexpr float kScale = 0.9f;           // CreateEffect's default
    static constexpr float kStride = 10.0f;         // units a frame, Direction (0, -10, 0)
    static constexpr float kForkSide = 200.0f;      // units east and west, on MU's own X
    static constexpr float kForkHeight = 700.0f;
    static constexpr int kStoneOdds = 4;            // rand_fps_check(4)
    // Ours: the faint light under it, minimal on purpose.
    static constexpr float kGlowTiles = 3.0f;
    static constexpr float kGlow[3] = {0.28f, 0.28f, 0.35f};

    struct Whirl {
        bool alive = false;
        float at[3] = {};
        float way[2] = {};  // world x, z a unit
        float yaw = 0.0f;
        float left = 0.0f;  // LifeTime, reference frames
        float key = 0.0f;   // its action's frame
        float smokeDue = 0.0f;
    };
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float velocity[3] = {};  // metres a second
        float size = 1.0f;
        float left = 0.0f;       // reference frames
        float spin = 0.0f;
    };
    static constexpr int kWhirls = 8;
    static constexpr int kPuffs = 128;

    std::vector<EffectCorner> poses_[kKeys];
    int keyCount_ = 0;
    // Two keys blended into here as each is drawn; sized once at open, so a frame allocates nothing.
    mutable std::vector<EffectCorner> blended_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Whirl whirls_[kWhirls] = {};
    Puff puffs_[kPuffs] = {};
    uint32_t dice_ = 0x570e4a11u;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float metres() const { return ground_ ? ground_->metresPerTile() : 1.0f; }
    float floorAt(float x, float z, float fallback) const {
        return ground_ ? ground_->heightAt(x, z) : fallback;
    }
    void puff(const Whirl& whirl);
};

template <typename Fork, typename Stone>
void Storm::update(float seconds, Fork fork, Stone stone) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    // A tile is a hundred of MU's units.
    const float perUnit = metres() / 100.0f;
    for (Whirl& whirl : whirls_) {
        if (!whirl.alive) continue;
        whirl.left -= frames;
        if (whirl.left <= 0.0f) {
            whirl.alive = false;
            continue;
        }
        whirl.key = std::fmin(float(keyCount_ - 1), whirl.key + kKeysPerFrame * frames);
        const float stride = kStride * perUnit * frames;
        whirl.at[0] += whirl.way[0] * stride;
        whirl.at[2] += whirl.way[1] * stride;
        whirl.at[1] = floorAt(whirl.at[0], whirl.at[2], whirl.at[1]);
        // A puff a frame at its foot.
        whirl.smokeDue -= frames;
        while (whirl.smokeDue <= 0.0f) {
            whirl.smokeDue += 1.0f;
            puff(whirl);
        }
        // The two bolts, each on a coin of one in two a frame.
        for (int side = -1; side <= 1; side += 2) {
            if (unit() >= 0.5f * frames) continue;
            const float from[3] = {whirl.at[0] + float(side) * kForkSide * perUnit,
                                   whirl.at[1] + kForkHeight * perUnit, whirl.at[2]};
            fork(from, whirl.at);
        }
        if (unit() < frames / float(kStoneOdds)) stone(whirl.at);
    }
    const float drag = std::pow(0.4f, frames);
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        for (int k = 0; k < 3; ++k) one.at[k] += one.velocity[k] * seconds;
        for (float& v : one.velocity) v *= drag;
        one.size += 0.1f * frames;
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
}

}  // namespace mu::game
