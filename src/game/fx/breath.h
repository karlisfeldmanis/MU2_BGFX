// What a Budge Dragon gives off: the fire it breathes as it bites and the dust it raises round
// itself while it lives. MU2's Breath and Dust, which are two blocks of MuMain's per-frame effect
// switch in ZzzCharacter.cpp, one falling into the other:
//
//   case MODEL_BUDGE_DRAGON:
//       if (o->CurrentAction == MONSTER01_ATTACK1 && o->AnimationFrame <= 4.f && rand_fps_check(1))
//           ... CreateParticle(BITMAP_FIRE, <32 to 64 units out from bone 7>, o->Angle, white, 1);
//       // no break
//   case MODEL_DARK_KNIGHT: case MODEL_LARVA: case MODEL_CHAIN_SCORPION:
//       if (c->Dead == 0 && rand_fps_check(4))
//           ... CreateParticle(BITMAP_SMOKE + 1, <within 32 units of it>, o->Angle, Light);
//
// So the dust is the Dark Knight's, reached by a missing break; it is what the client does and
// so what a player saw. Each particle below is ZzzEffectParticle.cpp's own motion for its type,
// kept in MU's units per 25 Hz reference frame and converted at the edge, so the numbers read
// against the source without arithmetic.
//
// **One departure, MU2's, and a single factor.** MU scales the model and not its particles, so
// a half-size dragon breathes full-size fire -- flames most of a metre across on an animal
// under one -- and raises the Dark Knight's two metres of dust. Every length here is taken at
// the size of the animal carrying it (`scale`); the lives, growth, speeds, drag and fade are
// MU's untouched.
//
// Pools sized once, and a frame allocates nothing: a spark or a puff past the pool is refused
// and counted, the rule every pool in this engine keeps.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Breath {
public:
    // Takes MU's Fire01 strip and smoke02 off the cooked showing. Not fatal: without a sheet
    // the dragon simply gives nothing off.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);
    void shutdown();

    // One BITMAP_FIRE, subtype 1, born at `at` (world metres) and thrown along `along`, the
    // monster's facing on the ground plane, unit length.
    void spark(const float at[3], const float along[2], float scale);
    // One BITMAP_SMOKE + 1 round a body standing at `feet`, drifting along `along`.
    void puff(const float feet[3], const float along[2], float scale);

    // Ages everything by the frame's own seconds.
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

    uint32_t live() const { return uint32_t(sparks_.size() + puffs_.size()); }
    uint32_t refused() const { return refused_; }

private:
    struct Spark {
        float position[3] = {0, 0, 0};
        float velocity[2] = {0, 0};  // metres a reference frame, on the ground plane
        float scale = 0.1f;          // MU's Scale, which grows by `gravity` every frame
        float gravity = 0.0f;        // MU's Gravity, which grows by 0.004 every frame
        float life = 24.0f;          // reference frames left
        float size = 1.0f;           // the animal's scale
    };
    struct Puff {
        float position[3] = {0, 0, 0};  // x and z; y is pinned to the ground each frame
        float velocity[2] = {0, 0};
        float scale = 0.5f;
        float life = 32.0f;
        float size = 1.0f;
    };

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle fire_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    std::vector<Spark> sparks_;
    std::vector<Puff> puffs_;
    uint32_t dice_ = 0x1b873593u;
    uint32_t refused_ = 0;
    bool open_ = false;

    uint32_t roll();
};

}  // namespace mu::game
