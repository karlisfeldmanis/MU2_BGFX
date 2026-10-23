// Fire Burst, the Dark Lord's: what MU draws as he lets it go (docs/plans/dark-lord-spells/
// fire-burst.md). MuMain's AT_SKILL_FIREBURST arm of the attack effects (ZzzCharacter.cpp:
// 5065-5086; Main 1.03.35 :5987), on PLAYER_ATTACK_STRIKE (80), let go on its key 3 of 7 as Force
// is (ZzzCharacter.cpp:2876-2883), eFirebustBoom cued there; eFirebust is the row's own, on the cast.
//
// **The heads.** Three MODEL_PIER_PART at sub-type 0 (Skill/PierPart.bmd's m_Piercing, an
// ellipsoid down its flight, Scale 1.2), set off his facing +90, 0 and -90 degrees, homing on
// the body he named -- the first on its crown, the other two on its middle -- for twenty frames
// whatever they meet: `MoveHumming` turns yaw and pitch at most Velocity degrees a step, Velocity
// +0.4 a step (+0.1 more under ten frames left), a coin a step lifts the pitch 20, and each step
// is 26 units down its heading; the steps a frame start at one and grow by 0.1 a frame
// (ZzzEffect.cpp:3381-3394, MoveHandlers.cpp:5598-5643, ZzzAI.cpp:134). No hit and no burst: the
// realm's blow is its own.
//
// **The trail.** Each step stamps a sub-type 1 where the head is: the crossed quads of
// m_Piercing02 at Scale 0.5, turned as the head was, its alpha (20 - the head's life) / 5, living
// out the head's life and fading / 1.3 a frame over the last five (ZzzEffect.cpp:3396-3405,
// MoveHandlers.cpp:5651-5657). Each lays one BITMAP_FIRE+1 (Effect/Fire02, additive, 64 units a
// Scale): twelve frames, Scale 0.8 grown by a gravity that grows 0.02 a frame, flown on down the
// head's heading at 9.6-11.7 units a frame x 1.05 a frame and lifted 20 x the gravity, its light
// its life x 0.2 (ZzzEffectParticle.cpp:569-574, 4767-4775) -- the fire the footage shows.
//
// **The flash.** Two MODEL_DARKLORD_SKILL (DarkLordSkill.bmd's one quad, KingS_R), additive
// (1, 0.6, 0.3), angled (45, +45, 0) and (45, -45, 0): Scale 0.2 grown by a velocity of 0.1 that
// grows 0.02 a frame, the light / 1.8 a frame under seven frames left of ten -- 1.03.35's
// (ZzzEffect.cpp:14791-14799; MuMain's move lost it).
//
// **Ours, and marked**: the flash is turned to his facing (MU's yaw is 0, so where it points is
// chance) and slid forward so none of it is behind where it is made -- the user's "only frontal";
// the place it is all made is 40 units ahead of him at 130 up, where MU takes Bip01 x (80,0,20) in
// the pose, a bone this engine does not read here; AttackTime is read as a share of the clip as
// played. The heads fly in MU's units in a frame laid on his line at the let-go.
//
// Presentation only: the blows and their damage are the realm's.
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

class FireBurst {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // A Fire Burst begun by `who` at `target`, its clip `clipSeconds` long.
    void begin(uint32_t who, uint32_t target, float clipSeconds);

    // Every frame. `place(id, feet, &tall, &yaw)` where a body is drawn, false for none;
    // `cue(id)` the let-go's sound.
    template <typename Place, typename Cue>
    void update(float seconds, Place place, Cue cue);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kClipFrames = 7.0f / 0.25f;          // 80's length in reference frames
    static constexpr float kLetGo = kClipFrames * 3.0f / 7.0f;  // its key 3 of 7
    static constexpr float kAhead = 40.0f, kUp = 130.0f;        // where it is made (ours)
    static constexpr float kHeadFrames = 20.0f, kHeadScale = 1.2f, kStep = 26.0f;
    static constexpr float kPieceScale = 0.5f;
    static constexpr float kFireFrames = 12.0f, kFireSize = 64.0f;
    static constexpr float kFlashFrames = 10.0f;

    // A frame laid on his line at the let-go: MU's caster at its origin facing -Y, x across, z up.
    struct Line {
        float origin[3] = {}, ahead[3] = {0.0f, 0.0f, 1.0f}, across[3] = {1.0f, 0.0f, 0.0f};
    };
    struct Charge {
        bool alive = false;
        uint32_t who = 0, target = 0;
        float frame = 0.0f, rate = 1.0f;
        float way[3] = {0.0f, 0.0f, 1.0f};
    };
    struct Head {
        bool alive = false;
        uint32_t target = 0;
        Line line;
        float at[3] = {}, angle[3] = {}, aim[3] = {};  // MU units in its line
        float aimUp = 0.0f;                             // over the body's feet, MU units
        float velocity = 10.0f, gravity = 2.0f, left = kHeadFrames, due = 0.0f;
    };
    struct Piece {
        bool alive = false;
        float at[3] = {}, x[3] = {}, y[3] = {}, z[3] = {};  // world, the model's axes
        float alpha = 0.0f, left = 0.0f;
    };
    struct Fire {
        bool alive = false;
        float at[3] = {}, way[3] = {};  // world metres, and its heading
        float speed = 0.0f, scale = 0.8f, gravity = 0.0f, spin = 0.0f, left = kFireFrames;
    };
    struct Flash {
        bool alive = false;
        Line line;
        float roll = 45.0f;
        float scale = 0.2f, velocity = 0.1f, light = 1.0f, left = kFlashFrames;
    };
    static constexpr int kCharges = 4;
    static constexpr int kHeads = 12;
    static constexpr int kPieces = 640;
    static constexpr int kFires = 640;
    static constexpr int kFlashes = 8;

    std::vector<EffectCorner> head_, piece_, flash_;
    bgfx::TextureHandle headSheet_ = BGFX_INVALID_HANDLE, pieceSheet_ = BGFX_INVALID_HANDLE,
                        flashSheet_ = BGFX_INVALID_HANDLE, fireSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Charge charges_[kCharges] = {};
    Head heads_[kHeads] = {};
    Piece pieces_[kPieces] = {};
    Fire fires_[kFires] = {};
    Flash flashes_[kFlashes] = {};
    uint32_t dice_ = 0x510e527fu;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    void letGo(const float feet[3], const float way[3], uint32_t target, float targetTall);
    void fly(Head& head, const float* there);
    void stamp(const Head& head);
    // A point of the line, MU units, to world metres; and a direction of it.
    void toWorld(const Line& line, const float mu[3], float out[3]) const;
    void wayToWorld(const Line& line, const float mu[3], float out[3]) const;
};

template <typename Place, typename Cue>
void FireBurst::update(float seconds, Place place, Cue cue) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    for (Charge& charge : charges_) {
        if (!charge.alive) continue;
        float feet[3], tall = 1.8f, yaw = 0.0f;
        if (!place(charge.who, feet, &tall, &yaw)) {
            charge.alive = false;
            continue;
        }
        // Down his line to what he named, flat; his facing when that body is gone.
        float there[3], high = 1.8f, turn = 0.0f;
        const bool named = charge.target != 0 && place(charge.target, there, &high, &turn);
        if (named) {
            const float dx = there[0] - feet[0], dz = there[2] - feet[2];
            const float far = std::sqrt(dx * dx + dz * dz);
            if (far > 0.05f) {
                charge.way[0] = dx / far;
                charge.way[2] = dz / far;
            }
        }
        charge.frame += frames * charge.rate;
        if (charge.frame >= kLetGo) {
            letGo(feet, charge.way, named ? charge.target : 0, high);
            cue(charge.who);
            charge.alive = false;
        }
    }
    // The heads, a reference frame at a time: their steering is MU's per frame.
    for (Head& head : heads_) {
        if (!head.alive) continue;
        float there[3], tall = 1.8f, yaw = 0.0f;
        const bool seen = head.target != 0 && place(head.target, there, &tall, &yaw);
        head.due += frames;
        while (head.alive && head.due >= 1.0f) {
            head.due -= 1.0f;
            fly(head, seen ? there : nullptr);
        }
    }
    for (Piece& one : pieces_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left < 5.0f) one.alpha *= std::pow(1.0f / 1.3f, frames);
        if (one.left <= 0.0f) one.alive = false;
    }
    const float metre = perUnit();
    for (Fire& one : fires_) {
        if (!one.alive) continue;
        one.gravity += 0.02f * frames;
        one.scale += one.gravity * frames;
        for (int k = 0; k < 3; ++k) one.at[k] += one.way[k] * one.speed * frames * metre;
        one.at[1] += one.gravity * 20.0f * frames * metre;
        one.speed *= std::pow(1.05f, frames);
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Flash& one : flashes_) {
        if (!one.alive) continue;
        one.scale += one.velocity * frames;
        one.velocity += 0.02f * frames;
        if (one.left < 7.0f) one.light *= std::pow(1.0f / 1.8f, frames);
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
}

}  // namespace mu::game
