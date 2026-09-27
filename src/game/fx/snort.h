// What a Bull Fighter blows out of its nose. MU2's Snort, which is one block of MuMain's
// per-frame effect switch in ZzzCharacter.cpp:
//
//   case MODEL_BULL_FIGHTER:
//       if (o->CurrentAction == MONSTER01_STOP1 && AnimationFrame in [15, 20]) Smoke = true;
//       if (o->CurrentAction == MONSTER01_STOP2 && AnimationFrame in [20, 25]) Smoke = true;
//       if (o->CurrentAction == MONSTER01_WALK && AnimationFrame in [2, 3] or [5, 6]) Smoke = true;
//       if (Smoke && rand_fps_check(2))
//           ... CreateParticle(BITMAP_SMOKE, <4 units down bone 24>, o->Angle, o->Light);
//
// Both variants: the switch tests o->Type, which is MODEL_BULL_FIGHTER for the plain bull and
// the Elite alike, and never looks at c->Level. Bone 24 is smok_bone, under the head -- the
// muzzle. The windows and the bone live with the caller, Play::snort; this is the particle.
//
// BITMAP_SMOKE subtype 0 is ZzzEffectParticle.cpp's own motion, kept in MU's units per 25 Hz
// reference frame. smoke01.jpg is a grey wisp on black with no alpha, so the particle pass
// draws it through EnableAlphaBlend -- (ONE, ONE), additive -- as it does every three-channel
// sheet. (BullFighter01.json's effects_from says "the ordinary alpha blend", which is wrong:
// RenderParticles picks the blend by the bitmap's Components, and a JPEG has three.)
//
// Unscaled by the animal, as MU draws it: the particle is its sheet's size times its own
// Scale. The bone point it is born on is posed with the figure and so already at the bull's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Snort {
public:
    // Takes smoke01 off the cooked showing. Not fatal: without it the bulls breathe nothing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One BITMAP_SMOKE, subtype 0, born at `at` (world metres).
    void puff(const float at[3]);

    void update(float seconds);
    void gather(gfx::Effects& effects) const;

    uint32_t live() const { return uint32_t(puffs_.size()); }
    uint32_t refused() const { return refused_; }

private:
    struct Puff {
        float position[3] = {0, 0, 0};
        float scale = 0.5f;    // MU's Scale, which grows by 0.05 a reference frame
        float gravity = 0.0f;  // MU's Gravity, which grows by 0.2 and lifts the puff by itself
        float life = 16.0f;    // reference frames left: MU's LifeTime
        float spin = 0.0f;     // MU's Rotation, fixed at birth for this subtype
    };

    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    std::vector<Puff> puffs_;
    uint32_t dice_ = 0x2545f491u;
    uint32_t refused_ = 0;
    bool open_ = false;

    uint32_t roll();
};

}  // namespace mu::game
