// A Kanturu Iron Rider's shows: the green sparks off its bow, the arrow it looses and the smoke it
// dies in.
//
// MuMain, GM_Kanturu_1st.cpp:
//
//   RenderKanturu1stMonsterVisual, MODEL_IRON_RIDER (:1246-1290), every frame it is not dying:
//   a CreateParticleFpsChecked(BITMAP_SPARK + 1, sub 10, Effect/Spark03) at IRON_RIDER_BOW_15 and
//   at IRON_RIDER_BOW_16 (bones 52 and 47, Bone15 and Bone16, the bow's two tips), up to 4 units
//   off, in (0.8, 1.0, 0.8). Sub 10 lives 20 frames at a scale of 1-1.4 losing 0.03 a frame, its
//   light falling by 1/1.08 a frame (ZzzEffectParticle.cpp:2188-2195, 6694-6702).
//   Dying: BITMAP_SMOKE + 3 sub 3 (Effect/Clud64, 60 frames, at 2 x 1.28-2.54) and sub 4
//   (Effect/WATERFALL3, 30 frames, at 1 x 1.28-2.54) off IRON_RIDER_BIP01 each frame, up to 125
//   units off across and 45 down, rising 0-5 a frame and turning, their light falling by 1/1.012
//   a frame (:1735-1757, 5935-5960).
//
//   AttackEffectKanturu1stMonster, MODEL_IRON_RIDER (:690-712): through either attack the same
//   spark at IRON_RIDER_BOW_6 (bone 42, Bone06, the nock), and at AttackTime 10 a
//   MODEL_IRON_RIDER_ARROW from it along the rider's facing: Effect/ironobj.bmd drawn added in
//   (0.8, 1.0, 0.8), 70 units a frame for 12 frames, its scale 0.5 growing by 1.05 a frame and its
//   light falling by 0.7 a frame over its last five; each frame an Impack01 sprite at 4 and a BowE
//   at 2.5 on it and a spark sub 10 off it (ZzzEffect.cpp:506-519; MoveHandlers.cpp:281-300;
//   ZzzObject.cpp:1664-1668).
//
// **Ours**: the arrow flies at what the rider hunts when it hunts one, along its facing when not,
// as Hell Maine's storm does, and ends where it strikes. Sparks at a third of MU's size and the arrow's two sprites at a third,
// the user's measure for the Aida sparks ('green bubles to large'). The arrow's MODEL_WAVES ring and
// its BITMAP_JOINT_HEALING trail are not carried. MU's numbers are per reference frame, 25 a
// second, in centimetres. Fed by Play::breedShows; nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Figure;

class IronRider {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One rider this frame: `attack` 1 or 2 while it swings (0 not), `key` its clip's key,
    // `yaw` its facing, `target` the feet of what it hunts or null, `dying` once it has fallen.
    void watch(uint32_t id, const Figure& figure, int attack, float key, float yaw,
               const float* target, bool dying, float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct One {
        bool found = false;
        bool seen = false;
        int bones[4] = {-1, -1, -1, -1};  // Bip01, the nock, the bow's two tips
        float owed = 0.0f;
        float frames = 0.0f;
        int lastAttack = 0;
        float lastKey = 0.0f;
        bool loosed = false;
    };
    struct Spark {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, light = 1.0f, age = 0.0f, fade = 1.0f;
    };
    struct Smoke {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, light = 1.0f, rise = 0.0f, spin = 0.0f, age = 0.0f, life = 60.0f,
              fade = 1.0f;
        bool cloud = true;  // Clud64; else WATERFALL3
    };
    struct Arrow {
        float at[3] = {0, 0, 0};
        float way[2] = {0, 1};
        float scale = 0.5f, light = 1.0f, age = 0.0f, owed = 0.0f, fade = 1.0f;
        float life = 12.0f;  // frames: MU's 12, or to its target
    };

    float unit();
    void spark(const float at[3], float fade);

    std::unordered_map<uint32_t, One> ones_;
    std::vector<Spark> sparks_;
    std::vector<Smoke> smokes_;
    std::vector<Arrow> arrows_;
    std::vector<EffectCorner> mark_, blur_;  // ironobj.bmd's two meshes
    uint32_t seed_ = 0x1e0a7d35u;
    int loosed_ = 0;  // the first few logged
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle impact_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bow_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloud_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spray_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle markSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blurSheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
