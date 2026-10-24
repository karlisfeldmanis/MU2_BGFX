// Critical Damage, the Dark Lord's: what MU draws for it (docs/plans/dark-lord-spells/
// critical-damage.md).
//
// **The cast.** PLAYER_SKILL_HAND, and 14 ticks after the echo -- Critical Damage has no
// AttackStage case, so AttackTime simply counts to 15 (ZzzCharacter.cpp:4133-4140) -- a
// MODEL_DARKLORD_SKILL at each hand holding a weapon (sub-type 0 the right, 1 the left; none on a
// shield), orange (1, 0.6, 0.3), and SOUND_CRITICAL (ZzzCharacter.cpp:4367-4384). The quad is
// DarkLordSkill.bmd's KingS_R, additive, angled (45, +-45, 0): Scale 0.2 grown by a velocity of 0.1
// that grows 0.02 a frame, the light / 1.8 a frame under seven of ten -- 1.03.35's move
// (ZzzEffect.cpp:14791-14799; MuMain's is dead code), as fx/fire_burst.h draws it.
//
// **While it stands**, on every player under it with a weapon in hand (not on a safe tile): every
// 1.2 s a BITMAP_FLARE_FORCE sub-type 1 at each weapon's link bone, which is three joints, 5, 6 and
// 7 (ZzzCharacter.cpp:10028-10070, ZzzEffect.cpp:3431-3436), and one pulse in twenty the orange
// flash too. A joint (ZzzEffectJoint.cpp:2540-2560, 6521-6583) lives fifteen frames and is laid
// again each frame from the bone: from bone x (0, 20, 0), `MaxTails` steps of (0, -4, 0) turned by
// the bone, the step 0.1 longer each; each step's point (0, 0, r) turned (phase +-40 a step, 0, 0)
// about the world's X -- phase 90, 180, 270 for 5, 6, 7, five and seven one way, six the other --
// r from 30 and 0.15 less a step for the joint's whole life; a BITMAP_FLARE of 0.5 on the line
// every other step. `MaxTails` one more a frame. Fire04 (BITMAP_FIRECRACKER), twenty units wide,
// light (1, 0.8, 1), / 1.5 a frame under seven. A bow's steps are nought: it coils at the grip.
//
// **Ours, and marked**: the flash turned to his facing and slid ahead of the hand -- the user's
// "only frontal" (MU's yaw is the world's 0); SOUND_CRITICAL on the cast alone, where MU plays it
// on every pulse of every buffed player (decision 7.8); the let-go at MU's 0.56 s read as a share
// of the clip as played.
//
// Presentation only: the bonus is the realm's.
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

class CriticalDamage {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // His cast, its clip `clipSeconds` long: the flash and the sound at the let-go.
    void begin(uint32_t who, float clipSeconds);
    // A body under it this frame, its weapons out: it pulses while it is handed in.
    void standing(uint32_t who);

    // Every frame. `hands(id, bones, bows)` his weapons' link bones, right first, up to two, and
    // whether each is a bow -- how many; `point(id, bone, local, out)` where `local`, metres in
    // that bone's frame, stands in the world; `facing(id, &yaw)` forward (sin yaw, 0, cos yaw);
    // `cue(id)` SOUND_CRITICAL.
    template <typename Hands, typename Point, typename Facing, typename Cue>
    void update(float seconds, Hands hands, Point point, Facing facing, Cue cue);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kLetGo = 0.56f / 0.83f;  // of the clip: 14 ticks of HAND's 0.83 s
    static constexpr float kPulseSeconds = 1.2f;
    static constexpr int kPulseFlashOneIn = 20;
    static constexpr float kFlashFrames = 10.0f;
    static constexpr float kCoilFrames = 15.0f, kCoilWidth = 20.0f, kFlareScale = 0.5f;
    static constexpr int kCoilMost = 30;  // `Weapon`, the cap on its steps

    struct Charge {
        bool alive = false;
        uint32_t who = 0;
        float left = 0.0f;  // seconds to the let-go
    };
    struct Wearer {
        uint32_t who = 0;
        float next = 0.0f;  // seconds to its next pulse
        bool seen = false;
    };
    struct Flash {
        bool alive = false;
        float at[3] = {}, ahead[3] = {0.0f, 0.0f, 1.0f}, across[3] = {1.0f, 0.0f, 0.0f};
        float roll = 45.0f;
        float scale = 0.2f, velocity = 0.1f, light = 1.0f, left = kFlashFrames;
    };
    struct Coil {
        bool alive = false;
        uint32_t who = 0;
        int bone = -1, kind = 5;
        bool bow = false;
        float radius = 30.0f, light = 1.0f, left = kCoilFrames, due = 0.0f;
        int tails = 1;  // `MaxTails`
        int count = 0, flares = 0;  // points laid this frame, and its flares on the line
        float points[kCoilMost][3] = {};
        float flare[kCoilMost][3] = {};
    };
    static constexpr int kCharges = 8, kWearers = 32, kFlashes = 32, kCoils = 96;

    std::vector<EffectCorner> flash_;
    bgfx::TextureHandle flashSheet_ = BGFX_INVALID_HANDLE, ribbonSheet_ = BGFX_INVALID_HANDLE,
                        flareSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Charge charges_[kCharges] = {};
    Wearer wearers_[kWearers] = {};
    Flash flashes_[kFlashes] = {};
    Coil coils_[kCoils] = {};
    uint32_t dice_ = 0x3c6ef372u;

    float unit();
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    void flash(const float hand[3], float yaw, bool left);
    void coil(uint32_t who, int bone, bool bow, int kind);
    template <typename Point>
    void lay(Coil& one, Point point);
    void ribbon(gfx::Effects& effects, const float (*points)[3], int count, float width,
                const float colour[3]) const;
};

template <typename Hands, typename Point, typename Facing, typename Cue>
void CriticalDamage::update(float seconds, Hands hands, Point point, Facing facing, Cue cue) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    // At each weapon in hand: a flash, and the coils.
    const auto atHands = [&](uint32_t who, bool flashes, bool coils) {
        int bones[2] = {-1, -1};
        bool bows[2] = {false, false};
        const int count = hands(who, bones, bows);
        float yaw = 0.0f;
        facing(who, &yaw);
        for (int h = 0; h < count && h < 2; ++h) {
            float hand[3];
            if (!point(who, bones[h], origin, hand)) continue;
            if (flashes) flash(hand, yaw, h == 1);
            if (coils) {
                for (int kind = 5; kind <= 7; ++kind) coil(who, bones[h], bows[h], kind);
            }
        }
    };
    for (Charge& one : charges_) {
        if (!one.alive) continue;
        one.left -= seconds;
        if (one.left > 0.0f) continue;
        one.alive = false;
        atHands(one.who, true, false);
        cue(one.who);
    }
    for (Wearer& one : wearers_) {
        if (one.who == 0) continue;
        if (!one.seen) {
            one = Wearer{};
            continue;
        }
        one.seen = false;
        one.next -= seconds;
        if (one.next > 0.0f) continue;
        one.next += kPulseSeconds;
        const bool wave = int(unit() * float(kPulseFlashOneIn)) == 0;
        atHands(one.who, wave, true);
    }
    for (Flash& one : flashes_) {
        if (!one.alive) continue;
        one.scale += one.velocity * frames;
        one.velocity += 0.02f * frames;
        if (one.left < 7.0f) one.light *= std::pow(1.0f / 1.8f, frames);
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    // The coils, a reference frame at a time: each is laid again from the bone every frame.
    for (Coil& one : coils_) {
        if (!one.alive) continue;
        one.due += frames;
        while (one.alive && one.due >= 1.0f) {
            one.due -= 1.0f;
            lay(one, point);
            if (one.left < 7.0f) one.light /= 1.5f;
            one.left -= 1.0f;
            if (one.left <= 0.0f) one.alive = false;
        }
    }
}

template <typename Point>
void CriticalDamage::lay(Coil& one, Point point) {
    const float metre = perUnit();
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float base[3];
    if (!point(one.who, one.bone, origin, base)) {
        one.alive = false;
        return;
    }
    // From bone x (0, 20, 0), down the bone's -Y a step at a time.
    const float startLocal[3] = {0.0f, 20.0f * metre, 0.0f};
    float start[3];
    point(one.who, one.bone, startLocal, start);
    float step = one.bow ? 0.0f : -4.0f;
    float phase = float(one.kind) * 90.0f;
    const int most = std::min(one.tails, kCoilMost);
    one.count = one.flares = 0;
    for (int i = 0; i < most; ++i) {
        const float stepLocal[3] = {0.0f, step * metre, 0.0f};
        float tip[3];
        point(one.who, one.bone, stepLocal, tip);
        for (int k = 0; k < 3; ++k) start[k] += tip[k] - base[k];
        phase += (one.kind % 2) != 0 ? 40.0f : -40.0f;
        // (0, 0, r) turned `phase` about MU's X: (0, -r sin, r cos) there, (0, r cos, r sin) here.
        const float turn = phase * 3.14159265358979f / 180.0f;
        one.points[one.count][0] = start[0];
        one.points[one.count][1] = start[1] + one.radius * std::cos(turn) * metre;
        one.points[one.count][2] = start[2] + one.radius * std::sin(turn) * metre;
        ++one.count;
        if ((i % 2) == 0) {
            for (int k = 0; k < 3; ++k) one.flare[one.flares][k] = start[k];
            ++one.flares;
        }
        one.radius -= 0.15f;
        if (!one.bow) step -= 0.1f;
    }
    one.tails = std::min(one.tails + 1, kCoilMost);
}

}  // namespace mu::game
