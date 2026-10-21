// Ice Storm's blizzard: MU's AT_SKILL_BLAST_FREEZE, ten MODEL_BLIZZARD crystals brought down on the
// tile it was thrown at (ZzzCharacter.cpp:5247-5277; the crystal's birth at ZzzEffect.cpp:3764-3797
// and its fall at :14262-14303). Here the Ancient rune Ice Storm's (sim/items.h kIceStorm...),
// said as Loosed with skill 39 (sim::skill::kIceStorm).
//
// **What is MU's, and kept.** The crystal: Blizzard01 on ice.png, added, at 0.5, born 600 units
// over the tile plus `(rand() % 50) * i` for the i-th, `rand() % 300 - 150` aside on both ground
// axes and a hundred more along X; falling at 30 to 59 units a frame and gaining 0 to 4 more a
// frame; its line moving ten units a frame against X, so all come in slantwise from one side, and
// shivering ten units about it; turned to the caster's angle; 15 to 29 frames to live, so the
// highest may go out in the air; its light rising a tenth a frame from nothing. Each frame of the
// fall: a smoke (BITMAP_SMOKE), a spark (BITMAP_ENERGY) or a flame (BITMAP_FIRE+2) by a coin, and
// a star (BITMAP_SHINY+1, (rand() % 4 + 4) * 0.2) and a glow (BITMAP_LIGHT) drawn on it in its
// light. On the ground: a blue smoke (0.24, 0.28, 0.8) half a metre up at (rand() % 32 + 80) *
// 0.025, one time in five an Ice shard (the caller's, fx/ice.h), and the crystal left standing
// twenty frames as its light falls by a tenth a frame. SOUND_SUDDEN_ICE1 at the let-go and
// SOUND_SUDDEN_ICE2 off the first crystal down are the caller's (Landing::first).
//
// **Ours.** The particles are this engine's sprites on MU's sheets at MU's sizes, not MU's
// particle movers: the smoke a frost wisp as Ice's (fx/ice.h), the spark and flame a short-lived
// mote. The second sound is played as the first crystal lands, not two frames after.
//
// Presentation only, `game` and not `sim`: the realm froze and struck everything on the tick it
// let the storm go. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Blizzard {
public:
    // Blizzard01 and ice.png out of effects/ice, the smoke, spark, flame, star and glow sheets off
    // the cooked showing. False when the crystal or its sheet is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // Ten crystals over `feet` (world metres, the ground there), turned to `yaw`, the caster's.
    void cast(const float feet[3], float yaw);

    // Where one came down this frame: the caller throws the shard and sounds the first.
    struct Landing {
        float x, y, z;   // world metres, the ground under it
        bool shard;      // one in five: an Ice shard off it (Ice::splinter)
        bool first;      // the storm's first crystal down: SOUND_SUDDEN_ICE2
    };
    void update(float seconds, std::vector<Landing>& landed);
    void gather(gfx::Effects& effects) const;

private:
    struct Crystal {
        bool alive = false;
        bool standing = false;  // down, MU's subtype 1
        bool first = false;     // the let-go's first (PKKey == 1)
        float at[3] = {};       // world metres
        float line[2] = {};     // its StartPosition, x and z
        float fall = 0.0f;      // MU units a frame, negative is down
        float yaw = 0.0f;
        float light = 0.0f;
        float left = 0.0f;      // reference frames
        float owed = 0.0f;      // frames of trail not yet thrown
        float spin = 0.0f;
    };
    struct Puff {
        bool alive = false;
        int kind = 0;           // 0 smoke, 1 spark, 2 flame, 3 ground smoke
        float at[3] = {};
        float size = 0.5f;
        float age = 0.0f;       // reference frames
        float life = 20.0f;
        float spin = 0.0f;
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr int kCrystalsACast = 10;
    static constexpr float kScale = 0.5f;
    static constexpr float kBirthHeight = 600.0f;     // MU units over the tile
    static constexpr float kDrift = 10.0f;            // units a frame against X
    static constexpr float kStandingFrames = 20.0f;
    static constexpr float kLightStep = 0.1f;
    static constexpr float kLightMost = 1.5f;
    static constexpr float kGroundSmoke[3] = {0.24f, 0.28f, 0.8f};
    static constexpr float kFrost[3] = {0.42f, 0.58f, 0.82f};

    static constexpr int kCrystals = 80;   // eight storms in the air at once
    static constexpr int kPuffs = 640;

    const content::Ground* ground_ = nullptr;
    std::vector<EffectCorner> crystal_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flame_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle star_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glow_ = BGFX_INVALID_HANDLE;
    Crystal crystals_[kCrystals] = {};
    Puff puffs_[kPuffs] = {};
    uint32_t dice_ = 0xB112A4D5u;
    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float floorAt(float x, float z) const;
    Puff* puff(int kind, const float at[3]);
};

}  // namespace mu::game
