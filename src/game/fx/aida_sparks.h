// The green sparks off a Death Rider's head and a Death Tree's horns.
//
// MuMain, RenderAidaMonsterVisual (GMAida.cpp):
//
//   MODEL_DEATH_RIDER (:872-881), every frame: Random_Light = rand() % 10 / 100 + 0.2, Light
//   (0, R, 0); 60 units along Monster102_Head (bone 6, Bip02 Neck) and 30 back,
//   CreateSprite(BITMAP_LIGHT + 1, Object9/Impack03, 3) and CreateParticleFpsChecked(BITMAP_SPARK
//   + 1, Effect/Spark03, sub 7): set up to 30 units off on MU's y and z (its x untouched: the
//   third offset writes Position[3]), LifeTime 30, Scale 1-1.4 losing 0.02 a frame, falling 1.3
//   units a frame, its light rolled (0, 0.2-0.69, 0) every frame (ZzzEffectParticle.cpp:2165-
//   2173, 6667-6675) -- a drizzle of green motes off its head.
//
//   MODEL_DEATH_TREE (:977-989), each of its six horns (Monster104_Horn0-5) every frame: a
//   BITMAP_LIGHT + 1 at 0.4 (ours: its star bones' starlight, play_show.cpp) and a BITMAP_SPARK +
//   1 sub 6 in (0.4, R, 0.5), R 0.8-0.89, which lives one frame in fifty: LifeTime 50, Scale
//   1-1.4, falling by a Gravity of 1-4.8 that gains 0.1 a frame and drifting 1 unit a frame on MU's
//   y, its green rolled 0.7-0.79 every frame (:2155-2164, 6657-6665) -- now and then a spark
//   dropping off a horn.
//
// All added billboards, Spark03 32 texels, Impack03 64, a texel a unit (ZzzEffectParticle.cpp:
// 8922-8935). **Ours**: the head's are laid at its aura bone (Bip02 Head), where MU's 60 along the
// neck lands; the sparks and the head's light at a third of MU's size (kSpark, kHeadScale: at MU's
// they read as green bubbles; the user, 'still tolarge').
// MU's numbers are per reference frame, 25 a second, in centimetres. Fed by Play::breedShows;
// nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class AidaSparks {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // A Death Rider's head this frame: its sprite, and its sparks for `seconds`.
    void head(const float at[3], float fade, float seconds);
    // One Death Tree horn this frame: one frame in fifty, a spark.
    void horn(const float at[3], float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Spark {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, gravity = 0.0f, age = 0.0f, life = 30.0f, fade = 1.0f;
        float tint[3] = {0, 0, 0};  // the colour less its green, which is rolled
        float green = 0.0f;
        bool horn = false;
    };
    struct Glow {
        float at[3];
        float light;
    };

    float unit();

    std::vector<Spark> sparks_;
    std::vector<Glow> glows_;   // this frame's only
    uint32_t seed_ = 0x7a1d5e03u;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle impact_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
