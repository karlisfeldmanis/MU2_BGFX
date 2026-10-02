// Death Stab's thrust: what MU draws while the knight drives a spear through one body. MuMain's
// AT_SKILL_DEATHSTAB arm of the attack effects (ZzzCharacter.cpp:2619-2697), on its own clip,
// PLAYER_ATTACK_DEATHSTAB (player.muc's 71) at 0.25, with SOUND_SKILL_SWORD2 (sKnightSkill2).
//
// Counted in AttackTime -- a frame a frame from the cast -- read here as a share of the clip:
// the thrust lands at half of it (the right hand from 77 units behind him to 71 ahead between
// keys 2 and 3 of 6; action 71 in source/players/rig/player.rig.json), AttackTime 12 of the 24
// frames the clip runs at 0.25. Three things, all MU's own, traced line by line after two cuts
// the user called buggy (2026-10-02: "go deeper in mumaiin and find what you do wrong"):
//
//   * **The streaks, AttackTime 2 to 8.** Three MODEL_SPEARSKILL sub-type 2 joints a frame
//     (ZzzCharacter.cpp:2636-2651). Each is born within three metres (GetNearRandomPos, a box
//     of +-300 units) of a point a metre and a fifth over him, pushed fourteen metres BEHIND him
//     -- `-sinf/+cosf` of his angle, the opposite of the gathering point's `+sinf/-cosf` -- and
//     is drawn over its first ten frames through him to three metres in front of his weapon's
//     link bone (`m_vPosSword`, :2653-2661), where it shrinks away over the rest of its twenty
//     (ZzzEffectJoint.cpp:4280-4297). A joint of five tails, width LifeTime * 3, NSkill
//     (BITMAP_FLARE_FORCE) at Light (1, 0.3, 0.3) (:1568-1572): on that blue sheet a dim violet.
//   * **The drill, AttackTime 6 to 12.** One frame in two, two MODEL_SPEARs -- the Light Spear,
//     an item model, which RenderEffects never draws: it has no render handler and the legacy
//     switch draws only MODEL_SKILL_BEGIN..END (ZzzEffect.cpp:8736-9647) -- at the link bone
//     pushed 100 + (AttackTime - 8) * 10 units ahead, living ten frames (EffectRegistry.cpp:57).
//     All that is seen of each is the BITMAP_FLARE joint at sub-type 12 and scale 100 it lays
//     EVERY frame (EffectBehaviors.cpp:88-94): Flare.jpg at Light (0.1, 0.1, 1), seventy frames
//     of LifeTime and fifty tails, a helix of 26 units' radius about a centre driven four units a
//     move along his facing and sinking (90 - LifeTime) * 0.3 (ZzzEffectJoint.cpp:1876-1886,
//     5559-5588) -- moved twice on every frame whose LifeTime is not a twelfth (:6942-6955), so
//     it lives about thirty-five frames and lays two tails a frame. A blue drill boring on ahead.
//   * **The wound.** From AttackTime 10 the body he named is `m_byHurtByDeathstab = 35`: for
//     thirty-five frames, one in two, a thin BITMAP_JOINT_THUNDER along every bone of it
//     (ZzzCharacter.cpp:4095-4117) -- here Thunder's fork between points of the body, its bones
//     not being known to this side.
//
// A joint's tail is a cross (CreateTailAxis, ZzzEffectJoint.cpp:2800-2830): a band across its
// angle's X and a band up its Z, each Scale wide, both drawn (RENDER_FACE_ONE | TWO), the sheet's
// U along the tails and V across (RenderJoints, :7036-7320). **A segment whose two tails' middles
// are more than sixty units apart is not drawn** (:7083-7087): the streaks cross seventeen metres
// in ten frames, so in flight they are all but unseen, and show as they settle at the point. A
// spear joint's light falls with its life, `min(LifeTime, 20) * 0.05` (:7006-7016).
// **Ours: the streaks are drawn whole and at full light anyway** -- with MU's skip and fade they
// were all but gone, and the user found the cut with them showing closer to MU (2026-10-02: "it
// was closer to MuMain before"). The drill keeps MU's skip; its segments are short.
//
// **The sound** is on AttackTime 8, not the swing's first key: `CheckAttackTime(8)` plays
// SOUND_SKILL_SWORD2 (ZzzCharacter.cpp:2629-2634), so it is cued from here (`cue`).
//
// **Ours, and marked**: aimed down his line to the body he named rather than off the drawn yaw,
// which is still coming round as the clip starts; the drill drawn at every second tail -- some
// seventy helices of fifty tails at its peak would take most of the frame's sprites -- and at a
// tenth of its light (kHelixLight).
//
// Presentation only: the blow and its damage are the realm's.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class DeathStab {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A Death Stab begun by `who` at `target`, its clip `clipSeconds` long.
    void begin(uint32_t who, uint32_t target, float clipSeconds);

    // Every frame. `place(id, feet, &tall, &yaw)` where a body is drawn, false for none;
    // `grip(id, out)` its weapon's link bone, false for none; `fork(from, to)` a thin bolt;
    // `cue(id)` the skill's sound, on AttackTime 8; `skeleton(id, pairs, most)` each bone and its
    // parent where they are drawn now, as many as fit, returning how many.
    template <typename Place, typename Grip, typename Fork, typename Cue, typename Skeleton>
    void update(float seconds, Place place, Grip grip, Fork fork, Cue cue, Skeleton skeleton);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kTwoPi = 6.28318531f;
    static constexpr float kClipFrames = 24.0f;   // 6 keys at 0.25: AttackTime's whole count

    // The streaks.
    static constexpr float kGatherFrom = 2.0f, kGatherTo = 8.0f;
    static constexpr int kStreaksPerFrame = 3;
    static constexpr float kStreakLift = 120.0f;   // CurPos[2] += 120
    static constexpr float kStreakScatter = 300.0f;
    static constexpr float kStreakBehind = 1400.0f;
    static constexpr float kGatherAhead = 300.0f;  // m_vPosSword
    static constexpr float kStreakFrames = 20.0f;
    static constexpr int kStreakTails = 5;
    static constexpr float kStreakWidth = 3.0f;    // units a frame of LifeTime
    static constexpr float kStreakLight[3] = {1.0f, 0.3f, 0.3f};
    static constexpr float kSoundAt = 8.0f;        // CheckAttackTime(8)
    static constexpr float kLongestSegment = 60.0f;  // units, RenderJoints' distSq test

    // The drill.
    static constexpr float kSpearFrom = 6.0f, kSpearTo = 12.0f;
    static constexpr float kSpearFrames = 10.0f;
    static constexpr float kHelixLife = 70.0f;
    static constexpr int kHelixTails = 50;
    static constexpr float kHelixWidth = 100.0f;   // the joint's Scale
    static constexpr float kHelixRadius = 40.0f * 0.65f;
    static constexpr float kHelixStride = 4.0f;    // units a move
    // MU's (0.1, 0.1, 1) at a tenth -- ours: some sixty helices start within a metre of each
    // other and drive on slowly, so thousands of their quads are added on one spot; under this
    // renderer's HDR and bloom that went past blue into a white-pink blot at full light and still
    // at a quarter, where MU's 8-bit target clamps at blue-white.
    static constexpr float kHelixLight[3] = {0.01f, 0.01f, 0.1f};
    static constexpr int kHelixDrawEvery = 2;      // ours: every second tail
    // **The drill is not drawn** -- ours, the user's (2026-10-02: "there is some effect which
    // happens after the cast, it looks little bi buggy"): its sixty-odd helices, MU's arithmetic
    // to the letter, piled under this renderer's HDR into a lavender, feathered wing hanging
    // beside him for over a second rather than MU's clamped blue. Kept, and off.
    static constexpr bool kDrill = false;

    // The wound.
    static constexpr float kWoundFrom = 10.0f;
    static constexpr float kWoundFrames = 35.0f;
    // MU's is one sub-type 7 bolt a bone, each bone to its parent as the body is posed now --
    // some thirty, a frame long, (0.5, 0.5, 1), scale 20, each end moved up to 20 units
    // (ZzzCharacter.cpp:4095-4117, ZzzEffectJoint.cpp:1157-1164): the body outlined in sparks,
    // on it as it falls. Here the same, bone to parent, but twelve of them chosen at random each
    // time, Thunder's pool being shared. (A first cut threw them in an upright cylinder where the
    // body stood, and they hung in the air over a fallen bull: the user, 2026-10-02, "after
    // effect still looks little bit wird".)
    static constexpr int kArcsAWound = 12;         // a frame in two
    static constexpr int kMostBones = 96;
    static constexpr float kBoneJitter = 20.0f;    // GetNearRandomPos(pos, 20)

    struct Charge {
        bool alive = false;
        uint32_t who = 0, target = 0;
        float frame = 0.0f;      // AttackTime
        float rate = 1.0f;       // AttackTime a reference frame: 24 over the clip
        float streakDue = 0.0f;
        float spearDue = 0.0f;
        bool wounded = false;
        bool sounded = false;
        float way[3] = {0.0f, 0.0f, 1.0f};  // flat, toward what he named
        float gather[3] = {};
    };
    // A joint: its tails, newest first, each a cross of two bands.
    template <int N>
    struct Ribbon {
        float side[N][2][3] = {};  // across his angle's X
        float rise[N][2][3] = {};  // up its Z
        int tails = 0;
        void push(const float at[3], const float across[3], float half) {
            for (int t = std::min(tails, N - 1); t > 0; --t) {
                for (int e = 0; e < 2; ++e) {
                    for (int k = 0; k < 3; ++k) {
                        side[t][e][k] = side[t - 1][e][k];
                        rise[t][e][k] = rise[t - 1][e][k];
                    }
                }
            }
            for (int k = 0; k < 3; ++k) {
                side[0][0][k] = at[k] - across[k] * half;
                side[0][1][k] = at[k] + across[k] * half;
                const float up = k == 1 ? half : 0.0f;
                rise[0][0][k] = at[k] - up;
                rise[0][1][k] = at[k] + up;
            }
            tails = std::min(tails + 1, N);
        }
    };
    struct Streak {
        bool alive = false;
        int charge = 0;
        float from[3] = {};
        float left = 0.0f;
        float moveDue = 0.0f;
        Ribbon<kStreakTails> ribbon;
    };
    struct Spear {
        bool alive = false;
        float at[3] = {};
        float way[3] = {};
        float left = 0.0f;
        float layDue = 0.0f;
    };
    struct Helix {
        bool alive = false;
        float centre[3] = {};
        float way[3] = {};
        float phase = 0.0f;
        float left = 0.0f;       // LifeTime
        float moveDue = 0.0f;
        Ribbon<kHelixTails> ribbon;
    };
    struct Wound {
        bool alive = false;
        uint32_t who = 0;
        float left = 0.0f;
        float due = 0.0f;
    };
    static constexpr int kCharges = 2;
    static constexpr int kStreaks = 64;
    static constexpr int kSpears = 16;
    static constexpr int kHelices = 96;
    static constexpr int kWounds = 6;

    bgfx::TextureHandle streakSheet_ = BGFX_INVALID_HANDLE, flare_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Charge charges_[kCharges] = {};
    Streak streaks_[kStreaks] = {};
    Spear spears_[kSpears] = {};
    Helix helices_[kHelices] = {};
    Wound wounds_[kWounds] = {};
    uint32_t dice_ = 0xd5a7b0e3u;
    float pairs_[kMostBones][2][3] = {};  // the wound's bones, filled each time it sparks

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    static void across(const float way[3], float out[3]) {
        out[0] = way[2];
        out[1] = 0.0f;
        out[2] = -way[0];
    }
    void moveHelix(Helix& one, float metre);
    template <int N>
    bool drawRibbon(gfx::Effects& effects, const Ribbon<N>& ribbon, bgfx::TextureHandle sheet,
                    const float colour[3], int every, bool skipLong) const;
};

template <typename Place, typename Grip, typename Fork, typename Cue, typename Skeleton>
void DeathStab::update(float seconds, Place place, Grip grip, Fork fork, Cue cue,
                       Skeleton skeleton) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    const float metre = perUnit();
    for (int c = 0; c < kCharges; ++c) {
        Charge& charge = charges_[c];
        if (!charge.alive) continue;
        float feet[3], tall = 1.8f, yaw = 0.0f, hand[3];
        if (!place(charge.who, feet, &tall, &yaw)) {
            charge.alive = false;
            continue;
        }
        if (!grip(charge.who, hand)) {
            for (int k = 0; k < 3; ++k) hand[k] = feet[k];
            hand[1] += tall * 0.6f;
        }
        // Down his line to what he named, flat; held where it was when that body is gone.
        float there[3], high = 0.0f, turn = 0.0f;
        if (charge.target != 0 && place(charge.target, there, &high, &turn)) {
            const float dx = there[0] - feet[0], dz = there[2] - feet[2];
            const float far = std::sqrt(dx * dx + dz * dz);
            if (far > 0.05f) {
                charge.way[0] = dx / far;
                charge.way[1] = 0.0f;
                charge.way[2] = dz / far;
            }
        }
        const float* way = charge.way;
        for (int k = 0; k < 3; ++k) charge.gather[k] = hand[k] + way[k] * kGatherAhead * metre;
        const float was = charge.frame;
        charge.frame += frames * charge.rate;
        if (!charge.sounded && charge.frame >= kSoundAt) {
            charge.sounded = true;
            cue(charge.who);
        }
        // The streaks: three a frame, born behind him.
        if (charge.frame >= kGatherFrom && was <= kGatherTo) {
            charge.streakDue -= frames * charge.rate * float(kStreaksPerFrame);
            while (charge.streakDue <= 0.0f) {
                charge.streakDue += 1.0f;
                for (Streak& one : streaks_) {
                    if (one.alive) continue;
                    one = Streak{};
                    one.alive = true;
                    one.charge = c;
                    for (int k = 0; k < 3; ++k) {
                        one.from[k] = feet[k] +
                                      between(-kStreakScatter, kStreakScatter) * metre -
                                      way[k] * kStreakBehind * metre;
                    }
                    one.from[1] += kStreakLift * metre;
                    one.left = kStreakFrames;
                    break;
                }
            }
        }
        // The spears: two, one frame in two, inside their window -- never drawn, only laid at.
        if (kDrill && charge.frame >= kSpearFrom && was <= kSpearTo) {
            charge.spearDue -= frames * charge.rate * 0.5f;
            while (charge.spearDue <= 0.0f) {
                charge.spearDue += 1.0f;
                const float out =
                    (100.0f + (std::min(charge.frame, kSpearTo) - 8.0f) * 10.0f) * metre;
                for (int n = 0; n < 2; ++n) {
                    for (Spear& one : spears_) {
                        if (one.alive) continue;
                        one = Spear{};
                        one.alive = true;
                        for (int k = 0; k < 3; ++k) {
                            one.at[k] = hand[k] + way[k] * out;
                            one.way[k] = way[k];
                        }
                        one.left = kSpearFrames;
                        break;
                    }
                }
            }
        }
        // The wound on what he named, once, from AttackTime 10.
        if (!charge.wounded && charge.frame >= kWoundFrom && charge.target != 0) {
            charge.wounded = true;
            for (Wound& one : wounds_) {
                if (one.alive && one.who != charge.target) continue;
                one = Wound{};
                one.alive = true;
                one.who = charge.target;
                one.left = kWoundFrames + (kSpearTo - kWoundFrom);
                break;
            }
        }
        // Kept past the clip while its streaks still need the gathering point.
        if (charge.frame >= kClipFrames + kStreakFrames) charge.alive = false;
    }
    // The streaks: through him to the gathering point over their first ten frames, a tail a frame.
    for (Streak& one : streaks_) {
        if (!one.alive) continue;
        const Charge& charge = charges_[one.charge];
        if (!charge.alive) {
            one.alive = false;
            continue;
        }
        one.moveDue -= frames;
        while (one.moveDue <= 0.0f && one.alive) {
            one.moveDue += 1.0f;
            const float reached = 1.0f - std::clamp((one.left - 10.0f) / 10.0f, 0.0f, 1.0f);
            float at[3], side[3];
            for (int k = 0; k < 3; ++k) {
                at[k] = one.from[k] + (charge.gather[k] - one.from[k]) * reached;
            }
            across(charge.way, side);
            one.ribbon.push(at, side, one.left * kStreakWidth * metre * 0.5f);
            one.left -= 1.0f;
            if (one.left <= 0.0f) one.alive = false;
        }
    }
    // The spears lay a helix every frame of their ten.
    for (Spear& one : spears_) {
        if (!one.alive) continue;
        one.layDue -= frames;
        while (one.layDue <= 0.0f && one.alive) {
            one.layDue += 1.0f;
            for (Helix& helix : helices_) {
                if (helix.alive) continue;
                helix = Helix{};
                helix.alive = true;
                for (int k = 0; k < 3; ++k) {
                    helix.centre[k] = one.at[k];
                    helix.way[k] = one.way[k];
                }
                helix.phase = float(int(unit() * 360.0f));
                helix.left = kHelixLife;
                break;
            }
            one.left -= 1.0f;
            if (one.left <= 0.0f) one.alive = false;
        }
    }
    // The helices: moved once a frame, and again unless LifeTime is a twelfth.
    for (Helix& one : helices_) {
        if (!one.alive) continue;
        one.moveDue -= frames;
        while (one.moveDue <= 0.0f && one.alive) {
            one.moveDue += 1.0f;
            moveHelix(one, metre);
            if (one.alive && int(one.left) % 12 != 0) moveHelix(one, metre);
        }
    }
    // The wound: thin bolts over the body, a frame in two.
    for (Wound& one : wounds_) {
        if (!one.alive) continue;
        one.left -= frames;
        float feet[3], tall = 1.8f, yaw = 0.0f;
        if (one.left <= 0.0f || !place(one.who, feet, &tall, &yaw)) {
            one.alive = false;
            continue;
        }
        one.due -= frames * 0.5f;
        while (one.due <= 0.0f) {
            one.due += 1.0f;
            // Each bone to its parent, on the body as it is drawn now, standing or falling.
            const int found = skeleton(one.who, pairs_, kMostBones);
            if (found <= 0) break;
            const float jitter = kBoneJitter * metre;
            for (int n = 0; n < std::min(kArcsAWound, found); ++n) {
                const int pick = std::min(found - 1, int(unit() * float(found)));
                float from[3], to[3];
                for (int k = 0; k < 3; ++k) {
                    from[k] = pairs_[pick][0][k] + between(-jitter, jitter);
                    to[k] = pairs_[pick][1][k] + between(-jitter, jitter);
                }
                fork(from, to);
            }
        }
    }
}

}  // namespace mu::game
