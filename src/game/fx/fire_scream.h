// Fire Scream, the Dark Lord's: what MU draws for it (docs/plans/dark-lord-spells/fire-scream.md).
//
// **The lanes.** At the strike's key 5 MU makes three pairs (ZzzCharacter.cpp:4500-4549; 1.03.35
// :5298-5347): one down his facing, one 80 units to each side turned 10 degrees out. Each pair is a
// MODEL_DARK_SCREAM_FIRE -- darkfirescrem01, a flame sheet (motion_blurr) and its streak
// (SwordEffor2), additive, Scale 2.3 less 0.14 a frame -- and a MODEL_DARK_SCREAM -- darkfirescrem02,
// chainring3's crossed flare, Scale 0.9 less 0.04 -- both hugging the ground, on 35 units a frame for
// their 19 frames (MoveHandlers.cpp:4626-4648, ZzzEffect.cpp:1648-1725). Each frame each head lets a
// BITMAP_FLAME sub-type 8 go at (Scale - 0.4) * 3.5 (Flame01; life 33, rising Gravity/2, shrinking
// Gravity/95, Gravity (1.8..2.79) * Scale, light / 1.007 a frame; ZzzEffectParticle.cpp:590-605,
// 4836-4852) -- the white-orange wall. The DARK_SCREAM lays BITMAP_JOINT_FORCE 7, Inferno's ribbon,
// 150 wide, 13 tails, life 20, its head racing on at a velocity of 10 that grows (ZzzEffectJoint.cpp:
// 2366-2378, 6419-6438), and one PoundingBall puff (life 30, scale 0.64-1.27 growing 0.19, rising 5).
//
// **Explosion** (79): three CreateBomb at the struck body + (rand % 160, rand % 160), 80 up -- twenty
// Spark02 sparks and one Explotion01 (the meteor's) and eExplosion (WSclient.cpp:4789-4802,
// ZzzEffect.cpp:6275-6347).
//
// **Ours, and marked**: each mesh is slid down its lane until its tail starts at his shoulders, and
// a flame never stands wider behind its head than the head is ahead of him -- the user's "only
// frontal" (MU's first frames reach six tiles behind him); the user's pick of 2026-10-24, "shift it
// forward", over cutting the sheet there, which left little of MU's streaks to see.
//
// Presentation only: who is struck and when is the realm's (Realm::fireScream).
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class FireScream {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // Let go at `feet` down `way` (the realm's aim, radians in the grid: east 0, the row's way a
    // quarter on).
    void loose(const float feet[3], float way);
    // An Explosion off the body standing at `at` (its feet).
    void explode(const float at[3]);

    // Every frame. `blast(at)` the meteor's Explotion01 at a bomb.
    template <typename Blast>
    void update(float seconds, Blast blast);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr int kFrames = 19;
    static constexpr float kStep = 35.0f, kFrom = 80.0f, kSide = 80.0f, kTurn = 10.0f;
    static constexpr float kFireScale = 2.3f, kFireShrink = 0.14f;
    static constexpr float kRingScale = 0.9f, kRingShrink = 0.04f;
    static constexpr int kTails = 13;
    static constexpr float kRibbonWidth = 150.0f;

    struct Lane {
        bool alive = false;
        float origin[3] = {};  // where it starts, on the ground
        float way[3] = {};     // its direction, flat
        float across[3] = {};
        float frame = 0.0f;    // reference frames since the let-go
        int laid = 0;          // frames whose flames are out
        // The ribbon: its head's distance down the lane, its speed, and its tails.
        float ribbon = 0.0f, speed = 10.0f, push = 1.0f;
        float tails[kTails] = {};
        int count = 0;
        float ribbonLight = 1.0f, ribbonLife = 20.0f, ribbonDue = 0.0f;
    };
    struct Flame {
        bool alive = false;
        float at[3] = {};
        float scale = 0.0f, gravity = 0.0f, light = 1.0f, life = 0.0f, spin = 0.0f;
    };
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float scale = 1.0f, life = 30.0f;
    };
    struct Spark {
        bool alive = false;
        float at[3] = {}, v[3] = {};
        float scale = 1.0f, life = 0.0f;
    };
    static constexpr int kLanes = 24, kFlames = 512, kPuffs = 24, kSparks = 240, kBombs = 24;

    std::vector<EffectCorner> sheet_, streak_, ring_;
    // How far each model trails behind its head, loaded metres at scale 1 (its largest Z).
    float sheetTail_ = 0.0f, ringTail_ = 0.0f;
    bgfx::TextureHandle sheetTex_ = BGFX_INVALID_HANDLE, streakTex_ = BGFX_INVALID_HANDLE,
                        ringTex_ = BGFX_INVALID_HANDLE, infernoTex_ = BGFX_INVALID_HANDLE,
                        flameTex_ = BGFX_INVALID_HANDLE, puffTex_ = BGFX_INVALID_HANDLE,
                        sparkTex_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Lane lanes_[kLanes] = {};
    Flame flames_[kFlames] = {};
    Puff puffs_[kPuffs] = {};
    Spark sparks_[kSparks] = {};
    float bombs_[kBombs][3] = {};
    int bombCount_ = 0;
    uint32_t dice_ = 0x510e527fu;

    float unit();
    float metre() const { return (ground_ ? ground_->metresPerTile() : 1.0f) * kUnit; }
    float floorAt(float x, float z) const { return ground_ ? ground_->heightAt(x, z) : 0.0f; }
    void head(const Lane& lane, float along, float out[3]) const;
    void step(float frames);
    void flame(const float at[3], float scale);
};

template <typename Blast>
void FireScream::update(float seconds, Blast blast) {
    seconds = std::fmin(seconds, 0.1f);
    for (int i = 0; i < bombCount_; ++i) blast(bombs_[i]);
    bombCount_ = 0;
    step(seconds * kReferenceFps);
}

}  // namespace mu::game
