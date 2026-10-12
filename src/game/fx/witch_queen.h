// A Witch Queen's lights, her blows' flashes and Attack 2's ring of strikes.
//
// MuMain, RenderAidaMonsterVisual's MODEL_WITCH_QUEEN (GMAida.cpp:703-795), her body
// (MONSTER_WITCH_QUEEN, GMAida.cpp:336-353, Monster101 at 1.4):
//
//   Every frame, Random_Light = rand() % 30 / 100 + 0.6 and Light (R + 0.5, R - 0.05, R + 0.5), a
//   magenta-white rolled a frame: CreateSprite(BITMAP_LIGHT + 1, Object9/Impack03) on her
//   registered bones L_Hand (76, Bip01 L Hand) at 0.8, z02-z05 (107-110, zx zx01 zx02 zx04) at
//   1.1, 1.0, 1.7 and 0.8 (:706-718); and at Footstepst (bone 0, Bip01) 65 down,
//   CreateParticleFpsChecked(BITMAP_LIGHT + 1, ..., (0.7, 0.5, 0.7), 4, 4): sub 4 wears
//   Effect/Shiny02, LifeTime 4, still (:720-723; ZzzEffectParticle.cpp:2003-2008, 6537).
//
//   ATTACK1 or ATTACK2 from AnimationFrame 4, 70 units off her feet (:753-764): BITMAP_SMOKE sub
//   27, Effect/Chrome03 at Scale 2 in (0.5, 0.2, 0.5), LifeTime 30, Light * 0.92 and Scale - 0.05
//   a frame (ZzzEffectParticle.cpp:1371-1375, 5529-5531); and BITMAP_LIGHT + 1 sub 2 at 1, white,
//   LifeTime 19, set up to 30 units aside, Scale * 0.92 a frame (:1993-1997, 6519-6522).
//
//   ATTACK1 from AnimationFrame 6 (:765-784): BITMAP_LIGHT + 1 sub 3 at 1.3 there and at her
//   target + 100 up: Shiny02, LifeTime 7, Scale * 1.3 and Light * 0.9 a frame (:1998-2002,
//   6523-6529). ATTACK2 from 6 (:786-788): BITMAP_JOINT_THUNDER sub 16 at her target, a short
//   crackle of JointThunder01 2-3 frames long (ZzzEffectJoint.cpp:1093-1096, 1303-1306).
//
//   ATTACK2, AttackTime 10 (nine frames in, whatever the clip's speed), AttackEffectAidaMonster
//   (:1464-1473): BITMAP_JOINT_FORCE sub 1 at her feet, unseen, LifeTime 20 (ZzzEffect.cpp:
//   4521-4528). At its LifeTime 10, 8, 6, 4 and 2 it turns 72 degrees from a random start and
//   drops two BITMAP_JOINT_THUNDER + 1 sub 6 on the point 250 units out (:8115-8128): each from
//   1 500 up, 45 tails 13 units down with 10 aside of jitter and five more closing on the point,
//   laid again every frame, 80 wide, LifeTime 20, Light / 1.2 a frame in its last four; where it
//   strikes, 30 over the ground, BITMAP_TRUE_BLUE (Effect/fantaB) at 2 every frame, rising 1-3
//   and drifting 2 a frame, LifeTime 24, Scale - 0.02; and Magic_Ground2 two tiles square on the
//   ground at 0.3, / 1.1 a frame (ZzzEffectJoint.cpp:1370-1419, 5089-5153, 7120-7125, 7334-7341;
//   ZzzEffectParticle.cpp:3357-3372, 8316-8317). Ten strikes in a five-point ring round her.
//
// The joints are not drawn here: each ring point's pair and Attack 2's crackle are handed to
// Play's Thunder (fx/thunder.h), which draws MU's JOINT_THUNDER for the wizard's Lightning -- a
// wide and a thin strip re-thrown every frame, JointThunder01 scrolling down them, a spark and a
// blue light where it bites -- and its `fork` for the crackle. The first cut drew its own single
// strips and the user found them 'cartoonish' (2026-10-12). Here are the rest: her lights, the
// blows' smoke and flashes, and under each strike the blue licks and the scorch.
//
// **Ours, marked.** Her lights and the foot glow at kLightDim of MU's (the monster auras' rule:
// subtle), the foot glow one sprite a frame where MU's four overlap; her 70 units ahead are in
// her facing; a strike lives Thunder's nine frames lit, not MU's twenty. MU's numbers are per
// reference frame, 25 a second, in centimetres. Fed by Play::breedShows from her own clip;
// nothing here reaches the sim.
#pragma once

#include <array>
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

class WitchQueen {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One queen this frame: which blow her clip is (0 none, 1 or 2), how far into it (keyOf),
    // her feet on the ground and her facing, her target's feet or null, her fade and the
    // frame's seconds.
    void watch(uint32_t id, const Figure& figure, int attack, float key, const float feet[3],
               float yaw, const float* target, float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;
    // A joint for Play's Thunder to draw: a strike from the sky (`loud`: the ring's first, its
    // thunder heard there), or a blow's crackle (`fork`). One at a time; false when none are left.
    struct Throw {
        float from[3], to[3];
        bool fork, loud;
    };
    bool take(Throw& out);

private:
    struct Mote {
        float at[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float scale = 1.0f, spin = 0.0f, light = 1.0f, age = 0.0f, life = 1.0f;
        enum class Kind : uint8_t { Smoke, Flash, Shine, Blue, Wisp } kind = Kind::Smoke;
    };
    // A ring point: waiting its turn, then struck, its licks and scorch for the strike's life.
    struct Strike {
        float at[3] = {0, 0, 0};   // 30 units over the ground, where MU's joints end
        float wait = 0.0f, age = 0.0f, light = 1.0f, scorch = 0.3f, blueOwed = 0.0f;
        bool loud = false;         // the ring's first: its thunder is heard where it falls
        bool thrown = false;
    };
    struct Queen {
        int bones[6] = {-1, -1, -1, -1, -1, -1};  // L Hand, zx, zx01, zx02, zx04, Bip01
        bool found = false, seen = false;
        float owed = 0.0f, lastKey = 0.0f, frames = 0.0f, roll = 0.8f, wispOwed = 0.0f;
        int lastAttack = 0;
        bool ring = false;  // this Attack 2's ring is cast
        bool shone = false; // this Attack 1's shines are thrown (ours: once, kImpact below)
        bool odd = false;   // every other frame's smoke and flash (ours)
    };
    struct Light {
        float at[3];
        float scale, roll, fade;
        bool foot;  // the Shiny02 glow at her feet, not an Impack03 light
    };

    float unit();
    void mote(Mote::Kind kind, const float at[3], float scale, float fade);
    void ring(const float feet[3], float fade);

    std::vector<Mote> motes_;
    std::vector<Strike> strikes_;
    std::vector<Throw> throws_;
    std::vector<Light> lights_;   // this frame's only
    std::unordered_map<uint32_t, Queen> queens_;
    uint32_t seed_ = 0x2c9e1d43u;
    bgfx::TextureHandle impact_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blue_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ground_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
