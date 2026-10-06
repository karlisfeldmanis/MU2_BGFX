// The wizard's Poison: MU's `MODEL_POISON` cloud where the body stands and ten smoke puffs thrown
// off it (ZzzCharacter.cpp:4993), and a green ground light while it lives.
//
// MU2's `client/core/Poison.cs` is the second source, ported: its numbers are MU's, traced there.
// What is MU's: Poison01's eleven poses at 0.7, stepped 0.3 keys a frame, forty frames, dimming
// over its last five; its two groups, wall01 drawn flat and wall02 added; the puffs, smoke01 at
// 0.8-1.1 m, born 32-96 units up and 32 aside, thrown out and up at 40-47 units a frame under a
// 0.4 drag, growing a twentieth a frame, fifty frames; the miasma light, (0.3, 1, 0.6) at two
// tiles, flickering 0.7-1.0.
//
// Ours: green fumes rising off the caster while he casts it (`fume`), as Ice's frost does.
// Presentation only. The blow and the pulses are the realm's (`SkillRow::poisonTicks`).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Poison {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    // A cloud on the body standing at `feet` (world metres), turned to the caster's `yaw`.
    void cast(const float feet[3], float yaw);
    // Fumes on the caster while he casts it: `feet` and his drawn `tall`. Every frame it runs.
    void fume(const float feet[3], float tall, float seconds);
    // **Sickness on a poisoned body** (the user, 2026-10-06: 'beter green smoke effect on monster
    // mesh to show poison efect not just simple tint'), ours: one slow green smoke puff born at
    // `at`, a point on its skeleton the caller draws at random, rising and opening as it fades.
    // `scale` its size for the body's (its height over a man's).
    void sicken(const float at[3], float scale = 1.0f);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

private:
    struct Cloud {
        bool alive = false;
        float at[3] = {};
        float yaw = 0.0f;
        float frame = 0.0f;
        float left = 0.0f;
        float glow = 1.0f;
    };
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float velocity[3] = {};  // metres a second
        float size = 1.0f;
        float age = 0.0f;
        float full = 50.0f;
        float spin = 0.0f;
        bool fume = false;
        bool sick = false;  // `sicken`'s
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kCloudScale = 0.7f;
    static constexpr float kKeysPerFrame = 0.3f;
    static constexpr float kCloudFrames = 40.0f;
    static constexpr float kDarkensUnder = 5.0f;
    static constexpr int kPuffs = 10;
    static constexpr float kFume[3] = {0.5f, 1.0f, 0.8f};
    static constexpr float kMiasma[3] = {0.3f, 1.0f, 0.6f};
    static constexpr float kMiasmaTiles = 2.0f;
    // The caster's fumes: smaller, fainter, a wisp every 1.5 reference frames.
    static constexpr float kFumeEvery = 1.5f;
    static constexpr float kFumeRadius = 0.35f;
    static constexpr float kCasterFume[3] = {0.25f, 0.6f, 0.3f};
    // A poisoned body's smoke: a deeper green than the spell's, rising slower and living
    // longer, 0.4-0.65 m on a man and opening as it goes. smoke01 is dark (its brightest texel
    // 71 of 255) and added, so it is lit past 1 to read on a lit body: 0.55 did not show on a
    // Stone Golem, 2.2 was 'little bit to much green smoke' (the user, 2026-10-06).
    static constexpr float kSick[3] = {0.40f, 1.0f, 0.35f};
    static constexpr float kSickBright = 1.4f;
    static constexpr float kSickRise = 0.35f;  // metres a second
    static constexpr float kSickFrames = 40.0f;

    static constexpr int kClouds = 24;  // the spell's 24 bodies at most (sim kVictims)
    static constexpr int kMaxPuffs = 512;  // 288 before a crowd could be poisoned (2026-10-06)

    std::vector<EffectCorner> flat_[11], lit_[11];
    int keyCount_ = 0;
    bgfx::TextureHandle wall1_ = BGFX_INVALID_HANDLE, wall2_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    Cloud clouds_[kClouds] = {};
    Puff puffs_[kMaxPuffs] = {};
    float fumeDue_ = 0.0f;
    uint32_t dice_ = 0xBADF00Du;
    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    Puff* freePuff();
};

}  // namespace mu::game
