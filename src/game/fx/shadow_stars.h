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
// BITMAP_ENERGY subtype 0 is Thunder01 at 0.6-1.3 of its 64 units, turned at random and turning
// 20 degrees a reference frame, for its two frames of life (ZzzEffectParticle.cpp:721-724, the
// CreateParticle default LifeTime 2).
//
// The caller finds the bones and feeds the points each frame; this draws them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class ShadowStars {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Forgets the last frame's stars and ages the sparks. Called before the feed.
    void update(float seconds);
    // One joint for this frame: a Shadow's dark star, or a Poison Shadow's green ring. `fade`
    // is the body's own, 0..1, so a corpse's go with it.
    void star(const float at[3], bool poison, float fade);
    // One BITMAP_ENERGY spark off a joint while it swings.
    void spark(const float at[3], bool poison);

    void gather(gfx::Effects& effects) const;

private:
    struct Star {
        float position[3];
        bool poison;
        float fade;
    };
    struct Spark {
        float position[3];
        float halfSize;
        float spin;   // radians
        float left;   // reference frames
        bool poison;
    };
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ring_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle energy_ = BGFX_INVALID_HANDLE;
    std::vector<Star> stars_;
    std::vector<Spark> sparks_;
    uint32_t dice_ = 0x9e3779b9u;
    bool open_ = false;
};

}  // namespace mu::game
