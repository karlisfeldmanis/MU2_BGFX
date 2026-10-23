// Force, the Dark Lord's primary: what MU draws as he lets it go. MuMain's AT_SKILL_FORCE arm of
// the attack effects (ZzzCharacter.cpp:5057-5063), on PLAYER_ATTACK_STRIKE (player.muc's 80, 7
// keys at 0.25), let go on its key 3 as every STRIKE skill is: `AnimationFrame >= 3` sets
// AttackTime to its limit, 15, and the let-go runs there (ZzzCharacter.cpp:2866-2873, 4140), with
// SOUND_ATTACK_SPEAR beside it, cued here. The realm lands the blow on the same key (the row's
// `release`, 3/7), so the number comes with the piercing.
//
// **The piercing.** One MODEL_PIERCING2 (Skill/m_Piercing.bmd): a cone and a white burst, Scale
// 2, 130 units over his feet along his line, its point flaring out behind him and its mouth
// opening towards what he struck. Ten frames; its light falls by 1.6 a frame while more than five are left,
// and on each of those frames it lays a sub-type 2 wave at Scale LifeTime x 0.05
// (ZzzEffect.cpp:3248-3262, Move_MODEL_PIERCING2, MoveHandlers.cpp:5360-5400).
//
// **The waves.** Two MODEL_WAVES at sub-type 1 (Skill/m_waves.bmd, cyan flames on a ring),
// stood on edge across his line (Angle[0] 90; the model lies flat) 130 units up and scattered 50 units, Scale 0.1-0.6 growing by a
// gravity that itself grows 0.07 a frame, to 2; fifteen frames, light / 1.5 a frame. A sub-type 2
// grows by 0.01 a frame to 1.5 (ZzzEffect.cpp:3190-3245, Move_MODEL_WAVES).
//
// **The streaks.** Each sub-type 1 wave lays two BITMAP_PIERCING joints where it was made: thirty
// tails of twenty units straight down his facing on their first frame, a line six tiles long of
// Effect/Piercing.jpg, warm (1, 0.8, 0.6) or white on a coin, fading / 1.4 a frame for ten
// (ZzzEffectJoint.cpp:2445-2465, 6481-6500). A tail is drawn as a cross of two bands, as MU's
// CreateTailAxis draws one.
//
// **Ours, and marked**: AttackTime is read as a share of the clip as it is played (his attack
// speed fits the clip), and his facing is the line to what he named, as Impale's is.
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

class Force {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // A Force begun by `who` at `target`, its clip `clipSeconds` long.
    void begin(uint32_t who, uint32_t target, float clipSeconds);

    // Every frame. `place(id, feet, &tall, &yaw)` where a body is drawn, false for none;
    // `cue(id)` the let-go's sound.
    template <typename Place, typename Cue>
    void update(float seconds, Place place, Cue cue);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kClipFrames = 7.0f / 0.25f;  // 80's length in reference frames
    static constexpr float kLetGo = kClipFrames * 3.0f / 7.0f;  // its key 3 of 7
    static constexpr float kUp = 130.0f;                // both, over his feet
    static constexpr float kScatter = 50.0f;            // rand() % 100 - 50
    static constexpr float kPierceScale = 2.0f, kPierceFrames = 10.0f, kPierceBright = 5.0f;
    static constexpr float kPierceFade = 1.6f;
    static constexpr float kWaveFrames = 15.0f, kWaveFade = 1.5f;
    static constexpr float kStreakTail = 20.0f;  // units a tail
    static constexpr int kStreakTails = 30;
    static constexpr float kStreakFrames = 10.0f, kStreakFade = 1.4f;

    struct Charge {
        bool alive = false;
        uint32_t who = 0, target = 0;
        float frame = 0.0f;  // AttackTime
        float rate = 1.0f;   // AttackTime a reference frame
        float way[3] = {0.0f, 0.0f, 1.0f};
    };
    struct Wave {
        bool alive = false;
        bool small = false;  // sub-type 2
        float at[3] = {}, way[3] = {0.0f, 0.0f, 1.0f};
        float scale = 0.0f, gravity = 0.0f, light = 1.0f, left = 0.0f;
    };
    struct Pierce {
        bool alive = false;
        float at[3] = {}, way[3] = {};
        float light = 1.0f, left = 0.0f, waveDue = 0.0f;
    };
    struct Streak {
        bool alive = false;
        float from[3] = {}, way[3] = {};
        float width = 0.0f, colour[3] = {}, left = 0.0f;
    };
    static constexpr int kCharges = 4;
    static constexpr int kWaves = 48;
    static constexpr int kPierces = 4;
    static constexpr int kStreaks = 16;

    std::vector<EffectCorner> wave_, cone_, burst_;
    bgfx::TextureHandle waveSheet_ = BGFX_INVALID_HANDLE, coneSheet_ = BGFX_INVALID_HANDLE,
                        burstSheet_ = BGFX_INVALID_HANDLE, streakSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Charge charges_[kCharges] = {};
    Wave waves_[kWaves] = {};
    Pierce pierces_[kPierces] = {};
    Streak streaks_[kStreaks] = {};
    uint32_t dice_ = 0x3c6ef372u;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    void letGo(const float feet[3], const float way[3]);
    void wave(const float at[3], const float way[3], bool small, float scale);
    void streak(const float from[3], const float way[3]);
};

template <typename Place, typename Cue>
void Force::update(float seconds, Place place, Cue cue) {
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
        float there[3], high = 0.0f, turn = 0.0f;
        if (charge.target != 0 && place(charge.target, there, &high, &turn)) {
            const float dx = there[0] - feet[0], dz = there[2] - feet[2];
            const float far = std::sqrt(dx * dx + dz * dz);
            if (far > 0.05f) {
                charge.way[0] = dx / far;
                charge.way[2] = dz / far;
            }
        }
        charge.frame += frames * charge.rate;
        if (charge.frame >= kLetGo) {
            letGo(feet, charge.way);
            cue(charge.who);
            charge.alive = false;
        }
    }
    // The piercing lays its small waves while it is bright.
    for (Pierce& one : pierces_) {
        if (!one.alive) continue;
        if (one.left > kPierceBright) {
            one.light *= std::pow(1.0f / kPierceFade, frames);
            one.waveDue += frames;
            for (; one.waveDue >= 1.0f; one.waveDue -= 1.0f) wave(one.at, one.way, true, one.left * 0.05f);
        }
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Wave& one : waves_) {
        if (!one.alive) continue;
        one.scale += one.gravity * frames;
        one.gravity += (one.small ? 0.01f : 0.07f) * frames;
        one.scale = std::fmin(one.scale, one.small ? 1.5f : 2.0f);
        one.light *= std::pow(1.0f / kWaveFade, frames);
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Streak& one : streaks_) {
        if (!one.alive) continue;
        for (float& c : one.colour) c *= std::pow(1.0f / kStreakFade, frames);
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
}

}  // namespace mu::game
