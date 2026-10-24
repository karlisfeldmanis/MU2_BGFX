// A Kanturu Kentauros's shows: the blue smoke off its bow while it shoots, the arrows it looses
// and the smoke it dies in.
//
// MuMain, GM_Kanturu_1st.cpp:
//
//   AttackEffectKanturu1stMonster, MODEL_KENTAUROS (:743-796): through either attack, every frame,
//   a BITMAP_SMOKE sub 31 (Effect/smoke01) at scale 2 in (0.3, 0.5, 0.7) on each of KENTAUROS_BIP_23
//   to _26 and _18 to _21 (bones 27-30 and 34-37, Bone23-26 and Bone18-21, its bow's two limbs).
//   Sub 31 lives 15 frames, losing 0.05 of scale and its light falling by 1/1.1 a frame
//   (ZzzEffectParticle.cpp:1400-1405, 5550-5557).
//   At AttackTime 14 a MODEL_KENTAUROS_ARROW: on Attack 1 one, off KENTAUROS_BIP_25 (Bone25); on
//   Attack 2, its multishot (ATMON_SKILL_NUM9, :604-614), three off KENTAUROS_BIP_23 (Bone23), along
//   its facing and 15 degrees either side.
//
//   The arrow (ZzzEffect.cpp:532-551; MoveHandlers.cpp:422-450): 34 frames. For its first ten it
//   waits where it was made; then it flies 70 units a frame along its facing, two BITMAP_SMOKE
//   sub 26 off it each frame in (0.3, 0.5, 1.0) -- 25 frames, at 0.64-1.28 growing 0.15 a frame, its
//   light multiplied by its frames left over 20 each frame (ZzzEffectParticle.cpp:1365-1370,
//   5524-5528) -- and a BITMAP_FLARE + 1 joint (Effect/Flare02, a white streak) of 16 tails at a
//   width of 15 units behind it, added (ZzzEffectJoint.cpp:2235-2241). Its cantasarrow.bmd is drawn
//   RENDER_BRIGHT at an alpha that starts at 0 and gains 0.001 a frame (ZzzObject.cpp:1675-1679):
//   never seen, so not carried.
//
//   RenderKanturu1stMonsterVisual, MODEL_KENTAUROS (:1373-1422): dying, every frame, BITMAP_SMOKE + 3
//   and + 1 at 0.3 off its tail (bones 81-83) and spine (4-6). Walking, its dust one frame in ten,
//   which Play::breedShows throws as the Blade Hunter's.
//
// **Ours**: the arrows fly at what it hunts when it hunts one, along its facing when not, the side
// two of a multishot 15 degrees off that line, and end at its distance: the Iron Rider's rule
// (fx/iron_rider.h). They leave at key 5.5, in the held full draw just before MU's release at 6.2
// (its AttackTime 14 and ten frames' wait), from where the bow's grip (Bone29, in its left hand) is then, not MU's Bone25 and
// Bone23, which are 40-60 cm out along its limbs, nor where MU made it, ten frames before; and fall to the chest of what they strike rather
// than flying level at a centaur's grip, over a man's head (the user, 2026-10-23: "release points
// of bolts could be better"). Smoke at a third of MU's size, the user's measure for monster sprites. The
// death's two smokes are both Clud64, as the Iron Rider's. MU's numbers are per reference frame,
// 25 a second, in centimetres. Fed by Play::breedShows; nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Figure;

class Kentauros {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One Kentauros this frame: `attack` 1 or 2 while it shoots (0 not), `key` its clip's key,
    // `yaw` its facing, `target` the feet of what it hunts or null, `dying` once it has fallen.
    void watch(uint32_t id, const Figure& figure, int attack, float key, float yaw,
               const float* target, bool dying, float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    static constexpr int kBow = 8;    // Bone23-26, Bone18-21
    static constexpr int kBack = 6;   // its tail's three and its spine's three
    struct One {
        bool found = false;
        bool seen = false;
        int grip = -1;  // Bone29, the bow's grip
        int bow[kBow];
        int back[kBack];
        float owed = 0.0f;
        int lastAttack = 0;
        float lastKey = 0.0f;
        bool loosed = false;
    };
    struct Smoke {
        float at[3] = {0, 0, 0};
        float colour[3] = {1, 1, 1};
        float scale = 1.0f, light = 1.0f, rise = 0.0f, spin = 0.0f, age = 0.0f, life = 15.0f,
              fade = 1.0f;
        int kind = 0;  // 0 the bow's (sub 31), 1 the arrow's (sub 26), 2 the death's cloud
    };
    struct Arrow {
        float from[3] = {0, 0, 0};
        float way[2] = {0, 1};
        float fall = 0.0f;  // metres down for each metre along
        float age = 0.0f, owed = 0.0f, fade = 1.0f;
        float flight = 24.0f;  // frames in the air: MU's 24, or to its target
    };

    float unit();
    void puff(const float at[3], int kind, float scale, const float colour[3], float fade);
    void loose(const float from[3], float yaw, const float* target, int count, float fade);

    std::unordered_map<uint32_t, One> ones_;
    std::vector<Smoke> smokes_;
    std::vector<Arrow> arrows_;
    uint32_t seed_ = 0x3c7a91e5u;
    int loosed_ = 0;  // the first few logged
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloud_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
