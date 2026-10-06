// Impale's thrust: what MU draws while the knight drives a spear through one body. MuMain's
// AT_SKILL_IMPALE arm of the attack effects (ZzzCharacter.cpp:2703-2759), on its own clip,
// PLAYER_ATTACK_SKILL_SPEAR (player.muc's 70, 8 key steps at 0.30, so AttackTime runs to some
// 27), with SOUND_RIDINGSPEAR on AttackTime 10 (`CheckAttackTime(10)`, :2706-2710), cued here.
//
// **The ghosts, AttackTime 13 and 14.** Three MODEL_SPEARSKILLs a frame -- RidingSpear01, a blue
// streak of a spear -- 145 units ahead and 110 up, each moved up to 30 units on every axis
// (:2735-2757), living twenty frames and driven five units a frame along his facing
// (ZzzEffect.cpp:641-645), Scale 1.5, drawn bright at (0.3, 0.3, 0.3) x LifeTime x 0.05
// (RenderSkillSpear, ZzzEffect.cpp:8683-8693): a flurry of spear-ghosts thrust on past what he
// struck.
//
// **Not drawn, and why.** The gathering on AttackTime 4 -- a MODEL__SPEAR at his weapon laying
// BITMAP_JOINT_HEALING sub-type 6 joints a metre out (Move_MODEL__SPEAR, MoveHandlers.cpp:
// 671-690) -- whose joints have no Velocity, so their four tails lie on one spot
// (ZzzEffectJoint.cpp:474-497); a first cut ran them in as sparks and they did not read at the
// game's distance (2026-10-06). And the cones on AttackTime 8, two MODEL_SPEARs 50 units ahead:
// Death Stab's invisible spears, whose helix the user turned off there (fx/deathstab.h, kDrill).
//
// **Ours, and marked**: AttackTime is read as a share of the clip as it is played -- on foot
// that is Death Stab's standing thrust (sim/skills.cpp's row), shorter than MU's ride clip -- and
// aimed down his line to the body he named, as Death Stab's is.
//
// Presentation only: the blow and its damage are the realm's.
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

class Impale {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // An Impale begun by `who` at `target`, its clip `clipSeconds` long.
    void begin(uint32_t who, uint32_t target, float clipSeconds);

    // Every frame. `place(id, feet, &tall, &yaw)` where a body is drawn, false for none;
    // `cue(id)` the skill's sound.
    template <typename Place, typename Cue>
    void update(float seconds, Place place, Cue cue);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kClipFrames = 8.0f / 0.30f;  // 70's AttackTime, MU's ride clip
    static constexpr float kSoundAt = 10.0f;
    static constexpr float kGhostFrom = 13.0f, kGhostTo = 15.0f;  // 13 <= AttackTime <= 14
    static constexpr int kGhostsAFrame = 3;
    static constexpr float kGhostAhead = 145.0f;
    static constexpr float kGhostUp = 110.0f;
    static constexpr float kGhostScatter = 30.0f;  // rand() % 60 - 30
    static constexpr float kGhostFrames = 20.0f;
    static constexpr float kGhostStride = 5.0f;    // units a frame
    static constexpr float kGhostScale = 1.5f;
    static constexpr float kGhostLight = 0.3f;

    struct Charge {
        bool alive = false;
        uint32_t who = 0, target = 0;
        float frame = 0.0f;  // AttackTime
        float rate = 1.0f;   // AttackTime a reference frame
        float ghostDue = 0.0f;
        bool sounded = false;
        float way[3] = {0.0f, 0.0f, 1.0f};
    };
    struct Ghost {
        bool alive = false;
        float at[3] = {}, way[3] = {};
        float left = 0.0f;
    };
    static constexpr int kCharges = 2;
    static constexpr int kGhosts = 24;

    std::vector<EffectCorner> spear_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Charge charges_[kCharges] = {};
    Ghost ghosts_[kGhosts] = {};
    uint32_t dice_ = 0x6a09e667u;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    Ghost* freeGhost();
};

template <typename Place, typename Cue>
void Impale::update(float seconds, Place place, Cue cue) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    const float metre = perUnit();
    for (Charge& charge : charges_) {
        if (!charge.alive) continue;
        float feet[3], tall = 1.8f, yaw = 0.0f;
        if (!place(charge.who, feet, &tall, &yaw)) {
            charge.alive = false;
            continue;
        }
        // Down his line to what he named, flat; held where it was when that body is gone.
        float there[3], high = 0.0f, turn = 0.0f;
        if (charge.target != 0 && place(charge.target, there, &high, &turn)) {
            const float dx = there[0] - feet[0], dz = there[2] - feet[2];
            const float far = std::sqrt(dx * dx + dz * dz);
            if (far > 0.05f) {
                charge.way[0] = dx / far;
                charge.way[2] = dz / far;
            }
        }
        const float from = charge.frame;
        charge.frame += frames * charge.rate;
        if (!charge.sounded && charge.frame >= kSoundAt) {
            charge.sounded = true;
            cue(charge.who);
        }
        // Three ghosts a frame on 13 and 14, ahead and up, scattered.
        if (charge.frame >= kGhostFrom && from < kGhostTo) {
            charge.ghostDue += frames;
            for (; charge.ghostDue >= 1.0f; charge.ghostDue -= 1.0f) {
                for (int g = 0; g < kGhostsAFrame; ++g) {
                    Ghost* one = freeGhost();
                    if (!one) break;
                    one->alive = true;
                    one->left = kGhostFrames;
                    for (int k = 0; k < 3; ++k) {
                        one->way[k] = charge.way[k];
                        one->at[k] = feet[k] + charge.way[k] * kGhostAhead * metre +
                                     between(-kGhostScatter, kGhostScatter) * metre;
                    }
                    one->at[1] += kGhostUp * metre;
                }
            }
        }
        if (from >= kGhostTo) charge.alive = false;
    }
    for (Ghost& one : ghosts_) {
        if (!one.alive) continue;
        for (int k = 0; k < 3; ++k) one.at[k] += one.way[k] * kGhostStride * metre * frames;
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
}

}  // namespace mu::game
