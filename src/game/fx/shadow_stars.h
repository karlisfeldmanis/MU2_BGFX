// What the Lost Tower's Shadows are made of. MuMain's RenderCharacter, case MODEL_SHADOW
// (ZzzCharacter.cpp:11310-11343):
//
//   for (int i = 0; i < b->NumBones; i++) {
//       if (!b->Bones[i].Dummy) {
//           if ((i >= 15 && i <= 20) || (i >= 27 && i <= 32)) {}      // the arms' claws
//           else {
//               b->TransformPosition(o->BoneTransform[i], p, Position, true);
//               if (c->Level == 0) CreateSprite(BITMAP_SHINY + 1, Position, 2.5f, Light, o, 0.f, 1);
//               else               CreateSprite(BITMAP_MAGIC + 1, Position, 0.8f, Light, o, 0.f);
//               if (rand_fps_check(4) && o->CurrentAction >= MONSTER01_ATTACK1
//                                     && o->CurrentAction <= MONSTER01_ATTACK2)
//                   CreateParticle(BITMAP_ENERGY, Position, o->Angle, Light);
//           }
//       }
//   }
//
// with Light (1, 1, 1) on the Shadow (Level 0) and (0.2, 0.7, 0.1) on the Poison Shadow
// (Level 1). A sprite of subtype 1 goes through EnableAlphaBlendMinus, so the Shadow's stars
// darken: a black X of Shiny02 (32 by 64 texels, so 0.8 by 1.6 m at 2.5) over every joint. The
// Poison Shadow's are Magic_Ground2's soft ring (128 square, 1.02 m at 0.8), added in green.
//
// **Ours**, the user's asks of 2026-10-01 ("more bluryy", "to active", "it has to be more
// subtle", "more elegant and subtle"): both sheets blurred, a dozen of the body's joints rather
// than thirty-nine (play_tuning.h kShadowJoints), both far fainter than MU's, and no
// BITMAP_ENERGY sparks.
//
// The caller finds the bones and feeds the points each frame; this draws them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class ShadowStars {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Forgets the last frame's stars. Called before the feed.
    void update();
    // One joint for this frame: a Shadow's dark star, or a Poison Shadow's green ring. `fade`
    // is the body's own, 0..1, so a corpse's go with it.
    void star(const float at[3], bool poison, float fade);
    // **Ours** (the user, 2026-10-01: "it also probably be a minimal light emiter"): a Poison
    // Shadow's middle, for a faint green light on what is round it. MU's MODEL_SHADOW case
    // lights nothing.
    void glow(const float at[3], float fade);
    // The nearest of this frame's glows to `near`, at most two, into the renderer's moving
    // lights; after the spells, which keep their slots.
    uint32_t lights(gfx::PointLight* out, uint32_t max, const float near[3]) const;

    void gather(gfx::Effects& effects) const;

private:
    struct Star {
        float position[3];
        bool poison;
        float fade;
    };
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ring_ = BGFX_INVALID_HANDLE;
    std::vector<Star> stars_;
    struct Glow {
        float position[3];
        float fade;
    };
    std::vector<Glow> glows_;
    bool open_ = false;
};

}  // namespace mu::game
