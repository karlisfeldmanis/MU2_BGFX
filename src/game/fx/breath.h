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
// so what a player saw.
//
// **`puff` has a second caller and the name of this file no longer covers it.** MODEL_GIANT's
// own case is `MonsterDieSandSmoke(o)` (ZzzCharacter.cpp:5552, :6178), which throws twenty of
// the identical `BITMAP_SMOKE + 1` round the body between keys 8 and 9 of its death clip. It is
// the same particle with the same scatter and the same drift, so it is the same pool: a second
// one would be a second sheet load and a second reserve for a thing that is not a second thing.
// Play::sandOnDeath is where its own timing lives. If a third caller arrives, this stops being
// "what a Budge Dragon gives off" and becomes what it already is -- MU's monster particles --
// and gets the rename then rather than now.
//
// Each particle below is ZzzEffectParticle.cpp's own motion for its type,
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

    // One puff of a Giant's death sand: the same particle and the same sheet, thrown OUTWARD
    // from the body on `out` rather than drifted along its facing, and dressed for a low cloud
    // that opens and settles rather than a dust trail that follows something.
    //
    // Everything about its dressing is **invention** and none of it is MU's -- the user asked
    // for the ground smoke to be elegant and not too much (2026-09-22), and MU's own numbers
    // read as a solid tan blob shoved to one side: twenty puffs born at half a metre across,
    // all given the same velocity along the body's facing, so they travel as one clump instead
    // of opening. What is kept of MU is the particle, the sheet, the ground pin, the fade and
    // the moment it is thrown. What is changed is how many, how big, how bright and which way.
    void sand(const float feet[3], const float out[2], float reach, float scale);

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
        // Over MU's own fade, and 1 for everything MU throws. The Giant's sand is the only
        // thing that asks for less; see sand().
        float alpha = 1.0f;
        // How fast it opens. MU grows every puff by 0.08 of its scale a reference frame; the
        // sand grows slower so a low cloud spreads outward instead of swelling upward.
        float growth = 0.08f;
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
