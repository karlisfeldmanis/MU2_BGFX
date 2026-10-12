// Hell Maine's lights, the fire off its hands, Attack 1's storm and Attack 2's fire lay.
//
// MuMain, RenderAidaMonsterVisual's MODEL_HELL_MAINE (GMAida.cpp:1002-1084), its body
// (MONSTER_HELL_MAINE, GMAida.cpp:406-422, Monster106 at 1.8):
//
//   Every frame, Light = (rand() % 8 / 10 + 0.6, 0, 0), a red rolled a frame: CreateSprite
//   (BITMAP_LIGHT + 1, Object9/Impack03) on its registered eyes R_Eye and L_Eye (9, 10: Bone02,
//   Bone03) at 0.3, and on its left arm's L_Arm00-02 (33-35: zx04, zx03, zx08) at 1 (:1007-1023).
//
//   Every frame, BITMAP_TRUE_FIRE (Effect/fantaF) sub 6 off both hands (20 and 39, Bip01 L Hand
//   and R Hand): at 5 each through ATTACK2, else 2.5 and 2.1 (:1059-1074). LifeTime 32, thrown
//   (rand() % 10 - 5) * 0.4 aside and (rand() % 10 + 5) * 0.2 up a frame, the aside * 0.95 a
//   frame, rising one more a frame, Scale - 0.02, its light LifeTime / 25 in grey, added
//   (ZzzEffectParticle.cpp:3335-3376, 8271-8336).
//
//   ATTACK1, AttackTime 10 (nine frames in), AttackEffectAidaMonster (:1486-1499): MODEL_STORM
//   sub 3 at its feet. Its loop means five, but CheckAttackTime(10) holds for the first only, so
//   one storm, straight ahead: never drawn (HiddenMesh -2), walking 12 units a frame for 60
//   (ZzzEffect.cpp:2028-2043). Each frame (MoveHandlers.cpp:3458-3469) it throws BITMAP_SMOKE
//   sub 28 (PoundingBall, added: LifeTime 12, Scale 2.3, - 0.4 a frame for three then * 1.2,
//   rising 33, light * 0.92), sub 29 (smoke01 taken out of the picture, the same) and sub 30
//   (smoke01 added, LifeTime 4, 1.8 + 0.15, rising 15), all 15 over the ground; one frame in two
//   a MODEL_STONE (Play's meteor stones); and lays PoundingBall on the ground two tiles square at
//   0.3, spun by WorldTime (ZzzEffect.cpp:8764-8773; ZzzEffectParticle.cpp:1376-1399, 5533-5549).
//
//   ATTACK2 at AnimationFrame 9-10 (:1076-1079): BITMAP_BOSS_LASER sub 1 at its right hand in its
//   facing, LifeTime 20, still: every frame twenty Fire02 at 3, 50 units apart out to 10 m, each
//   over a smoke01 decal four tiles square in (0.2, 0, 0) (ZzzEffect.cpp:978-1017, 8967-9006).
//
// **Ours, marked.** The lights at kLightDim of MU's (the monster auras' rule: subtle). The hand
// fire every frame as MU's, at kFireSize (a third) of its size and kFireLight of its light: at MU's light its
// 32 flames a hand piled into one white bloom in our HDR picture, and thinned to every other frame
// they read as single balls (the user, 2026-10-12: 'fire does not looks okey'; at half size still 'bubbles'). The storm's smoke
// every other frame. One laser a blow where MU lays one a frame for its two keys, its flames at
// kLaserSize and kLaserLight, fading over their last five frames, a decal under every fourth at two
// tiles. Both blows go at its target. Not carried: EarthQuake (the storm shakes MU's camera
// every frame it walks), the laser's terrain light, the 10 units off the right hand. MU's numbers
// are per reference frame, 25 a second, in centimetres. Fed by Play::breedShows from its own
// clip; nothing here reaches the sim (the realm's blow is a melee, docs/aida-port.md decision 10).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/quake.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Figure;

class HellMaine {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One Hell Maine this frame: which blow its clip is (0 none, 1 or 2), how far into it
    // (keyOf), its feet on the ground and its facing, its target's feet or null, its fade and the
    // frame's seconds. Its blows go at the target: MU turns a monster to it before it swings, and
    // the drawn facing went wide of the knight in the studio (2026-10-12).
    void watch(uint32_t id, const Figure& figure, int attack, float key, const float feet[3],
               float yaw, const float* target, float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;
    // Each fire lay's red on the ground, a line light down its length: MU's AddTerrainLight
    // (0.2, 0, 0) over 2 tiles under each of its twenty flames, 50 units apart, summed.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    // MU's EarthQuake while a storm walks, (rand() % 8 - 4) * 0.1 degrees of pitch every frame
    // (MoveHandlers.cpp:3458-3465), decayed by 0.2 a frame (MainScene.cpp:199); the meteor's
    // rule for whose (fx/quake.h): the hero's own, or within 5 m of him. MU shakes for any.
    float quakeDegrees() const { return quake_; }
    QuakeFor quakeFor;
    // A stone a storm kicks up, for Play's meteor to throw. False when none are left.
    bool takeStone(float out[3]);

private:
    struct Mote {
        float at[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float scale = 1.0f, spin = 0.0f, light = 1.0f, age = 0.0f, life = 1.0f;
        enum class Kind : uint8_t { Fire, Ball, Dark, Puff } kind = Kind::Fire;
    };
    struct Storm {
        float at[3] = {0, 0, 0};
        float way[2] = {0, 1};
        float age = 0.0f, owed = 0.0f, light = 1.0f;
        uint32_t owner = 0;
        bool odd = false;
    };
    struct Laser {
        float at[3] = {0, 0, 0};
        float way[2] = {0, 1};
        float floor = 0.0f, age = 0.0f, light = 1.0f;
    };
    struct One {
        int bones[7] = {-1, -1, -1, -1, -1, -1, -1};  // eyes, arm, L Hand, R Hand
        bool found = false, seen = false;
        float owed = 0.0f, lastKey = 0.0f, frames = 0.0f, roll = 0.6f;
        int lastAttack = 0;
        bool stormed = false, lasered = false;
    };
    struct Light {
        float at[3];
        float scale, roll, fade;
    };

    float unit();
    void mote(Mote::Kind kind, const float at[3], float scale, float light);

    std::vector<Mote> motes_;
    std::vector<Storm> storms_;
    std::vector<Laser> lasers_;
    std::vector<Light> lights_;   // this frame's only
    std::vector<float> stones_;   // x, y, z each
    std::unordered_map<uint32_t, One> ones_;
    uint32_t seed_ = 0x51a7e3c9u;
    float clock_ = 0.0f;
    float quake_ = 0.0f;
    bgfx::TextureHandle impact_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fire_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ball_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flame_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
