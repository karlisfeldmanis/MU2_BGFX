// Twisting Slash's wheel: copies of the knight's own weapon flung round him, flat and spinning,
// with sparks and smoke where each one passes. MuMain's MODEL_SKILL_WHEEL1 and WHEEL2.
//
// **What MU does, and all of it is kept.** `AT_SKILL_TWISTING_SLASH` plays
// PLAYER_ATTACK_SKILL_WHEEL (SkillCast.cpp:306-312) and, when the attack's clock reaches
// `g_iLimitAttackTime` -- fifteen reference frames, `AttackTime` counting up from one -- makes a
// WHEEL1 at his feet with his facing and the sound SOUND_SKILL_SWORD4 (ZzzCharacter.cpp:4411-4423).
//
//   * WHEEL1 lives five frames and draws nothing; every frame it makes a WHEEL2 with
//     `SubType = 4 - LifeTime`, which the move-then-age order makes -1, 0, 1, 2, 3
//     (MoveHandlers.cpp:2773, ZzzEffect.cpp:1858, :8573).
//   * WHEEL2 lives 25 frames. Its alpha is its subtype's -- 0.6, 0.5, 0.4 for 1 to 3 and the
//     default 1 for the first two -- so the wheel is two solid copies and a fading tail.
//   * Each frame it stands at his position plus (0, -150, 0) turned by its angle -- 180 for a
//     polearm -- and its angle turns 18 degrees. Every copy starts from the same facing a frame
//     after the last, so each trails the one before by 18 degrees (MoveHandlers.cpp:2780-2810).
//   * It is drawn as his right hand's weapon (`Weapon[0]`), raised 100, rolled 90 and spun a
//     further 30 degrees a frame about its own axis, at ItemObjectAttribute's scale, 0.8 or 0.7
//     for a polearm, at its own plus (RenderWheelWeapon, ZzzEffect.cpp:8616).
//   * Every frame at its foot: a SMOKE subtype 3 (Effect/smoke01, bluish, bursting out and
//     opening over ten frames), a 0.3 terrain light three tiles wide, four JOINT_SPARK streaks
//     thrown along its path (`Angle = (rand%60 - 30, 0, rand%30 + 90) + its own`) with a SPARK
//     mote on one in four, and a BITMAP_LIGHT (flare01) at Scale 2 in (1, 0.8, 0.6), 20 up.
//
// **Ours, and marked.** The sparks are calmed as Hanzo's are (fx/forge.h, "sparkles is too
// crazy"): one streak of MU's four, and a mote on one frame in two. The five terrain lights are
// one point light at the hub, since the transient list is short and five lights 18 degrees apart
// read as one. Nothing else: the count, the radius, the speeds and the alphas are MU's.
//
// No weapon blur rides the swing: MuMain's blur and its hit spark are both gated to
// PLAYER_ATTACK_SKILL_SWORD1..5 (ZzzCharacter.cpp:3853, :4885), and the wheel's action is past
// them. game/play_show.cpp leaves the streak off for this skill.
//
// Presentation only, `game` and not `sim`: the realm resolved the blow on the tick, and nothing
// rolled here reaches the seeded log. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/mesh.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/forge.h"
#include "game/shine.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Wheel {
public:
    // Takes smoke01 and flare01 off the cooked showing, and opens its spark pool on the forge's
    // sheets. A missing sheet draws nothing of its kind.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A wheel for `owner`, to start turning when his attack clock reaches MU's fifteen. `weapon`
    // is what his right hand holds; null throws the sparks and the smoke alone.
    void cast(uint32_t owner, const content::Mesh* weapon, const ShineLook& shine, bool polearm);

    // `where(id, feet, yaw)` stands the owner as he is drawn now -- the wheel follows him -- and
    // is false when he is gone, which ends his wheel. `began(id, feet)` is called on the frame a
    // wheel starts to turn, which is when MU plays its sound.
    template <typename Where, typename Began>
    void update(float seconds, Where where, Began began);

    // The copies, into the camera's list only: they cast no shadow.
    void gather(std::vector<gfx::Drawable>& out) const;
    void gatherEffects(gfx::Effects& effects, const float eye[3], const float near[3],
                       float daylight) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;
    static constexpr float kStartFrames = 14.0f;  // AttackTime from 1 to g_iLimitAttackTime's 15
    static constexpr int kCopies = 5;             // WHEEL1's LifeTime
    static constexpr float kCopyFrames = 25.0f;   // WHEEL2's
    static constexpr float kAlpha[kCopies] = {1.0f, 1.0f, 0.6f, 0.5f, 0.4f};
    static constexpr float kOrbitUnits = 150.0f, kPoleOrbitUnits = 180.0f;
    static constexpr float kOrbitTurn = 18.0f;  // degrees a frame
    static constexpr float kSpinTurn = 30.0f;   // and the weapon about itself, on top
    static constexpr float kRaiseUnits = 100.0f;
    static constexpr float kScale = 0.8f, kPoleScale = 0.7f;
    static constexpr float kFlareUnits = 20.0f;
    static constexpr float kFlareScale = 2.0f;
    static constexpr float kFlareColour[3] = {1.0f, 0.8f, 0.6f};
    static constexpr float kSheetMetres = 0.64f;  // a 64-texel sheet at Scale 1
    // The hub's light: MU's 0.3 a copy, summed, and three tiles past the orbit.
    static constexpr float kLightPerCopy = 0.3f;
    static constexpr float kLightTiles = 3.0f;
    static constexpr float kMoteChance = 0.5f;  // ours, see above

    struct Copy {
        bool alive = false;
        float age = 0.0f;  // reference frames since it was made
        float orbit = 0.0f;  // radians, MU's Angle[2]
        float foot[3] = {};  // where it stands, on his ground
        float transform[16] = {};
    };
    struct Turn {
        bool alive = false;
        bool turning = false;
        uint32_t owner = 0;
        const content::Mesh* weapon = nullptr;
        ShineLook shine;
        bool polearm = false;
        float wait = 0.0f;   // reference frames to the start
        float clock = 0.0f;  // reference frames since the start: WHEEL1's own life
        float facing = 0.0f;
        float feet[3] = {};
        float owed = 0.0f;  // reference frames of particles owed
        float glow = 1.0f;  // MoveEffect's Luminosity, rolled a frame
        int born = 0;
        Copy copies[kCopies];
    };
    struct Puff {
        float at[3] = {};
        float velocity[3] = {};  // metres a reference frame
        float scale = 1.0f;
        float life = 10.0f;
        float spin = 0.0f;
    };

    static constexpr int kTurns = 8;
    static constexpr int kPuffs = 192;

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    Forge sparks_;
    Turn turns_[kTurns] = {};
    Puff puffs_[kPuffs] = {};
    int puffCount_ = 0;
    uint32_t dice_ = 0x7a3c19e5u;

    float unit();
    void place(Turn& turn, Copy& copy) const;
    void emit(const Copy& copy);
    void advance(float frames);
};

template <typename Where, typename Began>
void Wheel::update(float seconds, Where where, Began began) {
    // A long hitch is capped, as the forge caps its own.
    const float frames = std::fmin(seconds, 0.1f) * kReferenceFps;
    sparks_.update(seconds);
    advance(frames);
    for (Turn& turn : turns_) {
        if (!turn.alive) continue;
        float yaw = 0.0f;
        if (!where(turn.owner, turn.feet, yaw)) {
            turn.alive = false;
            continue;
        }
        if (!turn.turning) {
            turn.wait -= frames;
            if (turn.wait > 0.0f) continue;
            // Made with his angle as it is at that frame, and never turned by him after.
            turn.turning = true;
            turn.facing = yaw;
            turn.clock = -turn.wait;
            turn.owed = 1.0f;
            began(turn.owner, turn.feet);
        } else {
            turn.clock += frames;
            turn.owed += frames;
            for (Copy& copy : turn.copies) {
                if (copy.alive) copy.age += frames;
            }
        }
        // A copy a frame for WHEEL1's five, each made at the facing and aged from its own birth.
        while (turn.born < kCopies && float(turn.born) <= turn.clock) {
            Copy& copy = turn.copies[turn.born];
            copy = Copy{};
            copy.alive = true;
            copy.age = turn.clock - float(turn.born);
            ++turn.born;
        }
        bool any = false;
        for (Copy& copy : turn.copies) {
            if (!copy.alive) continue;
            if (copy.age >= kCopyFrames) {
                copy.alive = false;
                continue;
            }
            any = true;
            place(turn, copy);
        }
        // What each copy throws, a reference frame at a time at any frame rate.
        while (turn.owed >= 1.0f) {
            turn.owed -= 1.0f;
            turn.glow = 0.7f + 0.1f * float(int(unit() * 4.0f));
            for (const Copy& copy : turn.copies) {
                if (copy.alive) emit(copy);
            }
        }
        if (!any && turn.born >= kCopies) turn.alive = false;
    }
}

}  // namespace mu::game
