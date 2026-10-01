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

    // Forgets the last frame's stars and ages the embers. Called before the feed.
    void update(float seconds);
    // One joint for this frame: a Shadow's dark star, or a Poison Shadow's green ring. `fade`
    // is the body's own, 0..1, so a corpse's go with it.
    void star(const float at[3], bool poison, float fade);
    // **Ours** (the user, 2026-10-01: "it also probably be a minimal light emiter"): a faint
    // light in `colour` at a monster's effect -- a Poison Shadow's green, a Death Gorgon's
    // orange, a Death Knight's sword (game/play_tuning.h kAuraLights).
    void glow(const float at[3], float fade, const float colour[3]);
    // One of a Death Gorgon's embers: MU's ten BITMAP_FIRE a frame on random bones (:6064-6071),
    // ours as one now and then, rising and burning out through Fire01's four frames.
    void ember(const float at[3]);
    // The Devil's beam for this frame only: MU's BITMAP_JOINT_LASER + 1 from a hand to the hero
    // (ZzzCharacter.cpp:2300-2311), ours as one faint strip a hand, drawn as two crossed quads.
    void beam(const float from[3], const float to[3]);
    // One of a Death Gorgon's Flame of Evil fireballs, rolling out along the ground from `at` the
    // way (dx, dz) points: MU's MODEL_FIRE subtype 1 (:1959-1968), ours as a Fire01 sprite.
    void roll(const float at[3], float dx, float dz);
    // A Balrog's Flame of Evil circle on the ground at `at`, spreading and fading: MU's
    // MODEL_CIRCLE and CIRCLE_LIGHT (:1976-1978), ours as a flat ring of Magic_Ground2 in orange.
    void circle(const float at[3]);
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
        float colour[3];
    };
    std::vector<Glow> glows_;
    struct Ember {
        float position[3];
        float age;   // reference frames
    };
    std::vector<Ember> embers_;
    bgfx::TextureHandle fire_ = BGFX_INVALID_HANDLE;
    struct Beam {
        float from[3], to[3];
    };
    std::vector<Beam> beams_;
    bgfx::TextureHandle laser_ = BGFX_INVALID_HANDLE;
    struct Roll {
        float position[3];
        float dx, dz;
        float age;   // seconds
    };
    std::vector<Roll> rolls_;
    struct Circle {
        float position[3];
        float age;   // seconds
    };
    std::vector<Circle> circles_;
    bool open_ = false;
};

}  // namespace mu::game
