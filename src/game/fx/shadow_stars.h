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
    // One of an Alquamos's star lights (ZzzCharacter.cpp:8792-8801): MU's BITMAP_LIGHT at Scale
    // 0.6 on a g_chStar bone, added in Luminosity * (0.8, 0.9, 1), Luminosity rolled 0.7-1 a
    // frame. Ours at kStarlightDim of that and the luminosity passed in, steadier (play_show).
    void starlight(const float at[3], float fade, float luminosity);
    // An Alquamos blow's ribbon for this frame: MU's BITMAP_FLARE sub 7 joint (ZzzEffectJoint.cpp:
    // 1924-1932, 5603-5729), its tails `points` (the newest first) as a strip of Flare.jpg 30
    // units wide in (0.2, 0.2, 1), and at its head a Shiny02 and two flare01 in (0.5, 0.5, 1).
    void ribbon(const float (*points)[3], int count, float fade);
    // One of a Death Gorgon's embers: MU's ten BITMAP_FIRE a frame on random bones (:6064-6071),
    // ours as one now and then, rising and burning out through Fire01's four frames.
    void ember(const float at[3]);
    // The Devil's beam for this frame only: MU's BITMAP_JOINT_LASER + 1 from a hand to the hero
    // (ZzzCharacter.cpp:2300-2311), ours as one faint strip a hand, drawn as two crossed quads.
    void beam(const float from[3], const float to[3]);
    // The Vepar's: MU's BITMAP_BLUR + 1 joints from its hands to the target (ZzzCharacter.cpp:
    // 2190-2200), a soft streak of motion_blur `half` metres either side of its line.
    void blurBeam(const float from[3], const float to[3], float half);
    // The Lizard King's: MU's BITMAP_JOINT_THUNDER from its hands to the target (ZzzCharacter.cpp:
    // 2330-2342), a streak of joint_thunder `half` metres either side of its line.
    void thunderBeam(const float from[3], const float to[3], float half, const float colour[3]);
    // One of a Death Gorgon's Flame of Evil fireballs, rolling out along the ground from `at` the
    // way (dx, dz) points: MU's MODEL_FIRE subtype 1 (:1959-1968), ours as a Fire01 sprite.
    void roll(const float at[3], float dx, float dz);
    // A Balrog's Flame of Evil circle on the ground at `at`, spreading and fading: MU's
    // MODEL_CIRCLE and CIRCLE_LIGHT (:1976-1978), ours as a flat ring of Magic_Ground2 in orange.
    void circle(const float at[3]);
    // The Hydra's gem for this frame only: MU's RenderLight on its bone 63 (ZzzCharacter.cpp:
    // 11216-11219, 8453-8463) -- lightning2 at scale 1 and Shiny03's streak at 4, added in
    // (1, 0.6, 0.4) times sin(WorldTime*0.002)*0.3 + 0.7. `pulse` is that luminosity.
    void flare(const float at[3], float fade, float pulse);
    // A Queen Rainer's blow (ZzzCharacter.cpp:1681-1695): twenty BITMAP_BLIZZARD on whom it struck
    // (ZzzEffect.cpp:2974-3001; MoveHandlers.cpp:5019-5045). Each is born 500 units over `at`,
    // within 100 either way and 100 east, waits LifeTime - 15 of its 15-29 frames, then falls 15
    // frames, 20 units and 2 more each frame, drifting 10 west a frame, a Shiny02 at Scale
    // 0.8-1.4 and a flare01 at 1 turned at random, in a grey that rises 0.1 a frame. `wait` holds
    // the whole shower back, seconds. Ours: no BITMAP_FIRE + 2 trail, no jitter, at kBlizzardDim.
    void blizzard(const float at[3], float wait);
    // What a monster's lightning leaves where it struck (the user, 2026-10-04: 'lightings always
    // has to leave some minimal smoke for every monster which uses lighting'): a few faint grey
    // wisps of MU's smoke01 rising off `at` and gone in about a second. Ours.
    void wisp(const float at[3]);
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
        float half = 0.0f;  // 0: the laser's own width; else a blur or thunder beam's
        bool thunder = false;
        float colour[3] = {0.0f, 0.0f, 0.0f};  // a thunder beam's own; the others kBeamDim grey
    };
    std::vector<Beam> beams_;
    bgfx::TextureHandle laser_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blur_ = BGFX_INVALID_HANDLE;  // trail_motion, BITMAP_BLUR + 1
    bgfx::TextureHandle thunder_ = BGFX_INVALID_HANDLE;  // joint_thunder
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
    struct Flare {
        float position[3];
        float level;   // fade times pulse
    };
    std::vector<Flare> flares_;
    struct Wisp {
        float position[3];
        float drift[2];
        float age;   // seconds
    };
    std::vector<Wisp> wisps_;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;      // smoke01, a grey wisp on black
    bgfx::TextureHandle lightning_ = BGFX_INVALID_HANDLE;  // lightning_2, BITMAP_LIGHTNING + 1
    bgfx::TextureHandle streak_ = BGFX_INVALID_HANDLE;     // shiny_03, BITMAP_SHINY + 2
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;      // light, flare01, BITMAP_LIGHT
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;      // flare, Flare.jpg, BITMAP_FLARE
    bgfx::TextureHandle shinyAdded_ = BGFX_INVALID_HANDLE; // shiny_02, BITMAP_SHINY + 1
    struct Starlight {
        float position[3];
        float level;
    };
    std::vector<Starlight> starlights_;
    static constexpr int kRibbonTails = 15;
    struct Ribbon {
        float points[kRibbonTails][3];
        int count;
        float fade;
    };
    std::vector<Ribbon> ribbons_;
    struct Shard {
        float start[3];
        float wait;     // seconds before it falls
        float frames;   // reference frames it has fallen, to 15
        float scale;    // the Shiny02's
        float spin;
    };
    std::vector<Shard> shards_;
    uint32_t dice_ = 0x5A17u;
    bool open_ = false;
};

}  // namespace mu::game
