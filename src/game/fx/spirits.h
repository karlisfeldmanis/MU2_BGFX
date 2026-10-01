// Evil Spirit: MU's spirits let go round the caster -- the wizard's spell, and the shield's Evil
// Spirit rune on a miss (sim/items.h kSpiritChance).
//
// What is MU's (ZzzCharacter.cpp:4585-4603, ZzzEffectJoint.cpp:642-650 and 3737-3810): eight
// `BITMAP_JOINT_SPIRIT` joints at sub-type 0, a wide one (Scale 80) and a thin one (20) set off a
// metre over him at each of the four quarters, SOUND_EVIL with them. Each lives 49 frames with a
// six-point tail, flies seventy units a frame, and is steered back toward his middle (80 units up)
// by MoveHumming at ten degrees a frame -- which at that speed is a loop about four metres round
// him -- with a random wobble on its heading and pitch, kept between one and four metres over the
// ground. They are drawn subtracted (RENDER_TYPE_ALPHA_BLEND_MINUS): dark wisps, their light the
// LifeTime over ten, so they fade over their last ten frames. JointSpirit01 is the sheet.
//
// Not built: the darkening each lays on the ground under it (AddTerrainLight), and the wide
// joint's MODEL_LASER head -- Skill/Laser01.bmd, a dragon's skull under dragon02.OZJ, drawn
// subtracted at 1.3 (ZzzEffect.cpp:1832-1839). Built and taken out on 2026-10-01: subtracted in
// HDR its noisy sheet read as a dark scribble ("looks buged, was better before"). The strikes are
// the realm's (`Realm::letSpiritsGo`): what they do to a monster is any spell's -- the number
// and a flinch.
#pragma once

#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Spirits {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);
    // Eight joints off body `caster`, whose feet are at `feet` now.
    void release(uint32_t caster, const float feet[3]);
    // `where(id, feet)` puts a body's feet in `feet` and says whether it is still drawn; the
    // spirits steer for the last place they had while it is not.
    template <typename Where>
    void update(float seconds, Where where);
    void gather(gfx::Effects& effects) const;

private:
    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kLife = 49.0f;            // frames
    static constexpr float kSpeed = 70.0f * kUnit;   // metres a frame
    static constexpr float kTurn = 10.0f;            // degrees a frame, MoveHumming's
    static constexpr float kAim = 80.0f * kUnit;     // over his feet
    static constexpr float kStart = 100.0f * kUnit;  // set off this high
    static constexpr float kLowest = 100.0f * kUnit, kHighest = 400.0f * kUnit;
    static constexpr float kWide = 80.0f * kUnit, kThin = 20.0f * kUnit;
    static constexpr int kTails = 6;
    static constexpr int kMost = 8 * 6;  // six releases in the air at once
    // **Ours**: how much of what is behind a joint it takes away at its full light. MU's is one
    // (its Light clamps at LifeTime / 10); kept, for the user to judge in game.
    static constexpr float kDark = 1.0f;

    struct Joint {
        bool alive = false;
        uint32_t caster = 0;
        float aim[3] = {};      // his middle, last known
        float at[3] = {};
        float yaw = 0.0f, pitch = 0.0f;      // degrees: heading on the ground, and up
        float turnYaw = 0.0f, turnPitch = 0.0f;  // the wobble's own drift
        float width = kWide;
        float left = kLife;     // frames
        float owed = 0.0f;      // part of a frame not yet flown
        float tail[kTails][3] = {};
        int tails = 0;
    };
    void fly(Joint& joint);

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    Joint joints_[kMost];
    uint32_t dice_ = 0x9e3779b9u;
};

template <typename Where>
void Spirits::update(float seconds, Where where) {
    for (Joint& joint : joints_) {
        if (!joint.alive) continue;
        float feet[3];
        if (where(joint.caster, feet)) {
            joint.aim[0] = feet[0];
            joint.aim[1] = feet[1] + kAim;
            joint.aim[2] = feet[2];
        }
        joint.owed += seconds * kFps;
        while (joint.owed >= 1.0f && joint.alive) {
            joint.owed -= 1.0f;
            fly(joint);
        }
    }
}

}  // namespace mu::game
