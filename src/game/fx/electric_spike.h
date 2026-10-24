// Electric Spike, the Dark Lord's: what MU draws as he throws it (docs/plans/dark-lord-spells/
// electric-spike.md), on PLAYER_SKILL_FLASH (152, thirteen keys at 0.4), every piece on the clip's
// own keys (ZzzCharacter.cpp:10512-10560, 2885-2895, 4391-4404; Main 1.03.35 the same).
//
// **The gather**, at key 1.2: BITMAP_GATHERING sub-type 2 rides his hand for twenty frames,
// laying three BITMAP_SHINY+1 stars a frame on it ((rand() % 8 + 8) x 0.2) and, every other frame,
// three thin BITMAP_JOINT_THUNDER zaps (sub-type 3: ten units wide, ten frames, light 0.5, 0.5, 1)
// from a point 120 units out into the hand (MoveHandlers.cpp:1604-1660). sDarkElecSpikeReady is
// the skill row's own sound, on the cast.
//
// **The bolt**, at the let-go (key 5.5, the realm's `Loosed`): BITMAP_FLARE_FORCE, made 90 units
// ahead of him and 100 up, lays five joints of JointThunder01 down his aim (ZzzEffectJoint.cpp:
// 2489-2582, 6521-6667). The main one (sub-type 0, 250 units wide) runs MultiUse - 1 steps a frame,
// MultiUse 1 and +2 a frame, each step a tail and a stride of a velocity that starts at 3 units and
// grows 2 a step -- some 900 units in six frames, its thirty tails laid. The four others (100 wide)
// do the same while they coil round it: 80 units out, turned 20 degrees a step about his line,
// the radius 2.5 shorter a step, two of them the mirror of the other two, and the first two two to
// four frames late. Twenty frames (the late ones longer), the light (1, 0.8, 1) / 1.3 a frame under
// ten; the sheet runs along each once a second. sDarkElecSpike there, cued by the caller.
//
// **The rings**, at key 7: two MODEL_DARKLORD_SKILL sub-type 2 (KingS_R, additive 0.8, 0.5, 1) at
// his weapon hand, angled (180, 45, 0) and (0, 0, his facing): Scale 0.2 grown by a velocity of 0.4
// that falls 0.06 a frame, the light / 1.5 a frame under eight of twelve -- Main 1.03.35's move
// (ZzzEffect.cpp:14800-14807; MuMain's is empty).
//
// **Ours, and marked**: the rings laid on his line and slid ahead so none of them is behind his hand
// -- the user's "only frontal", where MU's crescent flares beside and behind him; no party wisps,
// as there is no party drain to draw (decision 6); the zaps' walk drawn as a jagged line re-thrown
// each frame; keys read as a share of the clip as played.
//
// **Ours, for "the number comes with the visual"**: a body WebZen's table strikes off the bolt --
// beside him, or wide of its coils -- takes a zap off the bolt into it as it is struck (`arc`): the
// bolt's sheet and light, sixty units wide, ten frames.
//
// Presentation only: the blows are the realm's.
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

class ElectricSpike {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // A cast begun by `who`, its clip `clipSeconds` long: the gather and the rings run on it.
    void begin(uint32_t who, float clipSeconds);
    // The bolt let go by `who` standing at `feet`, down `way` (the realm's aim, radians).
    void loose(uint32_t who, const float feet[3], float way);
    // A body struck off the bolt `wait` seconds on: a zap off the bolt, `from`, into it, `to`.
    void arc(const float from[3], const float to[3], float wait);

    // Every frame. `grip(id, out)` his weapon hand, false for none.
    template <typename Grip>
    void update(float seconds, Grip grip);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kClipFrames = 13.0f / 0.4f;  // 152's length in reference frames
    static constexpr float kGatherKey = 1.2f, kRingKey = 7.0f;
    static constexpr float kAhead = 90.0f, kUp = 100.0f;  // the bolt's start, MU units
    static constexpr int kTails = 30;

    struct Charge {
        bool alive = false, gathered = false, ringed = false;
        uint32_t who = 0;
        float key = 0.0f, rate = 1.0f;
        float way[3] = {0.0f, 0.0f, 1.0f};
    };
    struct Gather {
        bool alive = false;
        uint32_t who = 0;
        float at[3] = {};
        float left = 20.0f, due = 0.0f;
        float star[3][2] = {};  // the frame's stars: scale, spin
    };
    struct Zap {
        bool alive = false;
        float from[3] = {}, to[3] = {};
        float left = 10.0f;
        uint32_t seed = 0;
        float wait = 0.0f;   // seconds before it is drawn
        float width = 10.0f; // MU units
        bool bolt = false;   // the bolt's colour, not the gather's
    };
    // One joint of the bolt, in MU units on its line: x across, -y ahead, z up.
    struct Joint {
        bool alive = false;
        int kind = 0;  // MU's sub-type, 0 the main
        float origin[3] = {}, ahead[3] = {}, across[3] = {};  // the line, world
        float start[3] = {}, at[3] = {};
        float velocity = -3.0f, steps = 1.0f, radius = 80.0f, turn = 180.0f, wait = 0.0f;
        float width = 100.0f, light = 1.0f, left = 20.0f, due = 0.0f;
        int tails = 0;
        float tail[kTails][3] = {};
    };
    struct Ring {
        bool alive = false;
        float at[3] = {}, ahead[3] = {}, across[3] = {};
        float pitch = 0.0f, roll = 0.0f;
        float scale = 0.2f, velocity = 0.4f, light = 1.0f, left = 12.0f;
    };
    static constexpr int kCharges = 4, kGathers = 4, kZaps = 48, kJoints = 20, kRings = 8;

    std::vector<EffectCorner> ring_;
    bgfx::TextureHandle boltSheet_ = BGFX_INVALID_HANDLE, starSheet_ = BGFX_INVALID_HANDLE,
                        ringSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Charge charges_[kCharges] = {};
    Gather gathers_[kGathers] = {};
    Zap zaps_[kZaps] = {};
    Joint joints_[kJoints] = {};
    Ring rings_[kRings] = {};
    float clock_ = 0.0f;  // seconds, for the sheet's run along the bolt
    uint32_t dice_ = 0x6a09e667u;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    void gather(uint32_t who, const float hand[3]);
    void gatherFrame(Gather& one);
    void ring(const float hand[3], const float way[3]);
    void step(Joint& one);
    void jointWorld(const Joint& one, const float mu[3], float out[3]) const;
    void ribbon(gfx::Effects& effects, const float (*points)[3], int count, float width,
                const float colour[3], float scroll) const;
};

template <typename Grip>
void ElectricSpike::update(float seconds, Grip grip) {
    seconds = std::fmin(seconds, 0.1f);
    clock_ += seconds;
    const float frames = seconds * kReferenceFps;
    for (Charge& charge : charges_) {
        if (!charge.alive) continue;
        float hand[3];
        if (!grip(charge.who, hand)) {
            charge.alive = false;
            continue;
        }
        charge.key += frames * charge.rate * 0.4f;  // keys: the clip plays 0.4 a reference frame
        if (!charge.gathered && charge.key >= kGatherKey) {
            charge.gathered = true;
            gather(charge.who, hand);
        }
        if (!charge.ringed && charge.key >= kRingKey) {
            charge.ringed = true;
            ring(hand, charge.way);
            charge.alive = false;
        }
    }
    for (Gather& one : gathers_) {
        if (!one.alive) continue;
        grip(one.who, one.at);
        one.due += frames;
        while (one.alive && one.due >= 1.0f) {
            one.due -= 1.0f;
            gatherFrame(one);
        }
    }
    for (Zap& one : zaps_) {
        if (!one.alive) continue;
        if (one.wait > 0.0f) {
            one.wait -= seconds;
            continue;
        }
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Joint& one : joints_) {
        if (!one.alive) continue;
        one.due += frames;
        while (one.alive && one.due >= 1.0f) {
            one.due -= 1.0f;
            step(one);
        }
    }
    for (Ring& one : rings_) {
        if (!one.alive) continue;
        one.scale += one.velocity * frames;
        one.velocity -= 0.06f * frames;
        if (one.left < 8.0f) one.light *= std::pow(1.0f / 1.5f, frames);
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
}

}  // namespace mu::game
