// Fire Slash's look: the Magic Gladiator's ribbon of fire round his front, the lightning
// gathering at his hand before it, and three small skulls over a body whose defence it cut.
// docs/plans/mg-skills/fire-slash.md section 2 is the trace; this is its section 5.7.
//
// **What MU does, and all of it is kept.** AttackStage (ZzzCharacter.cpp:2783-2818) on a Fire
// Slash, at MU's 25 frames a second:
//
//   * **AttackTime 1 and 2, the gathering**: two BITMAP_GATHERING sub 1, a frame apart, each
//     twenty frames long (ZzzEffect.cpp:1295-1310, MoveHandlers.cpp:1603-1647). Every frame each
//     picks three points on a sphere of 120 units round his hand (bone 33 plus 10 up): on an
//     even LifeTime a blue BITMAP_JOINT_THUNDER sub 3 from the point in to the hand (scale 10,
//     ten frames, ten units a step, ten tails), on an odd one a BITMAP_SPARK + 1 (Spark03) at
//     the point that reaches the hand in ten frames (life 15, scale 0.10-0.59 growing 0.05 a
//     frame, light LifeTime / 5); and a BITMAP_SHINY + 1 sprite (Shiny02) on the hand, scale
//     1.6-3.0, rolled at random.
//   * **Key 3 of WHEEL, the ribbon**: SOUND_SKILL_SWORD3, and BITMAP_SWORD_FORCE, whose first
//     move makes BITMAP_JOINT_FORCE (ZzzEffectJoint.cpp:2277-2295, 6233-6365): a pivot 100 up
//     over where he stands, fixed there; LifeTime 20, eighteen tails. Each frame it lays eight
//     tails while it has fewer than seventeen -- a point 145 out from the pivot, then the way on
//     turned 11 degrees -- from +75 of his facing round his front to -112, 187 degrees in three
//     frames. Each tail is a band 150 across its spawn angle's X, which does not turn with the
//     sweep, so the crescent thins at its ends. At each step a BITMAP_FIRE (Fire01) on a coin
//     (life 24, scale 1.28-1.91, drifting 3.2-4.7 units a frame the joint's way, rising by a
//     Gravity of 0.004 a frame) and two blue JOINT_THUNDER sub 3 from the last point to this
//     one (scale 5-14 and 4-11, five frames). Its light is LifeTime / 30 down to 10, then
//     divided by 1.5 a frame; a tail's is ((18 - j) / 18) * 2 of that, the newest brightest. The
//     sheet is motion_blur_r2, U along the tails (the head at 1), V across.
//   * **The skulls** (ReceiveMagic, WSclient.cpp:4364-4381; MoveHandlers.cpp:583-634): while a
//     body's eDeBuff_Defense stands, three BITMAP_SKULL sprites (Skull, 16 units) 200 of its
//     scale over its feet, 120 degrees apart at a fixed turn, each 50 + 20 sin(i * 15.37 +
//     WorldTime * 0.0031) out, breathing; SOUND_BLOODATTACK on the body as it is laid.
//
// **Ours, and marked.** The joints' walk is MoveJoint's homing step with its jitter, a fresh
// turn each step, but drawn as one crossed strip (as fx/thunder.h draws MU's), its last two
// frames fading. Colours are clamped to one, as MU's glColor clamps -- under this renderer's HDR
// an unclamped 2x head blooms white. MU's Shiny02 is its pixels times Scale times an
// AnimationFrame running 0.2 to 1; drawn here at that run's middle, 0.6. No light: MU adds none.
// The swing's two paces are game/play.cpp's (SkillRow::paceAfter); the blow is the realm's.
//
// Presentation only, `game` and not `sim`: nothing rolled here reaches the seeded log. Pools are
// sized once and a frame allocates nothing.
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

class FireSlash {
public:
    // The ribbon, the flames, the bolts, the sparks, the hand's glow and the skull, off the
    // showing's table. False when the ribbon's sheet could not be had.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A Fire Slash by `owner`: the gathering from now, the ribbon `release` seconds on, where he
    // stands and faces then -- his clip's key 3 as it is played.
    void cast(uint32_t owner, float release);

    // Every frame. `place(id, feet, &yaw)` where a body is drawn, false for none; `hand(id, out)`
    // its weapon's link bone, false for none; `sound(id, feet)` SOUND_SKILL_SWORD3 as a ribbon is
    // made; `cuts(lay)` calls `lay(crown)` for each body whose defence stands cut, `crown` the
    // point 200 of its scale over its feet.
    template <typename Place, typename Hand, typename Sound, typename Cuts>
    void update(float seconds, Place place, Hand hand, Sound sound, Cuts cuts);

    void gatherEffects(gfx::Effects& effects) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;
    // ---- the gathering (BITMAP_GATHERING sub 1) ---------------------------------------------
    static constexpr int kGatherLife = 20;
    static constexpr int kGatherings = 2;           // AttackTime 1 and 2
    static constexpr int kGatherPoints = 3;
    static constexpr float kGatherRadius = 120.0f;
    static constexpr float kHandLift = 10.0f;
    static constexpr float kShinyLeast = 1.6f, kShinyMost = 3.0f;
    static constexpr float kShinyFrame = 0.6f;      // ours: AnimationFrame's middle, see above
    static constexpr float kShinyWide = 32.0f, kShinyTall = 64.0f;
    static constexpr float kSparkLife = 15.0f, kSparkPixels = 32.0f;
    static constexpr float kSparkGrows = 0.05f;
    // ---- the ribbon (BITMAP_JOINT_FORCE sub 0) ----------------------------------------------
    static constexpr int kRibbonLife = 20;
    static constexpr int kTails = 18;               // MaxTails; NumTails stops at 17
    static constexpr int kStepsAFrame = 8;
    static constexpr float kPivotLift = 100.0f;
    static constexpr float kRadius = 145.0f;
    static constexpr float kHalfWidth = 75.0f;      // a tail's cross, +-75 on its X
    static constexpr float kStartTurn = 75.0f;      // HeadAngle 45 + 30
    static constexpr float kStepTurn = 11.0f;
    // ---- its fire (BITMAP_FIRE sub 0) -------------------------------------------------------
    static constexpr float kFireLife = 24.0f, kFirePixels = 64.0f;
    static constexpr float kFireShrinks = 0.04f, kFireRises = 0.004f;
    // ---- the bolts (BITMAP_JOINT_THUNDER sub 3) ---------------------------------------------
    static constexpr int kBoltTails = 10;
    static constexpr float kBoltStep = 10.0f;       // Velocity, units a move
    static constexpr float kBoltFade = 2.0f;        // ours: its last two frames dimming
    static constexpr float kBoltBlue[3] = {0.5f, 0.5f, 1.0f};
    static constexpr float kBoltRepeats = 2.0f, kBoltScroll = 1.0f;
    // ---- the skulls (BITMAP_SKULL sub 0) ----------------------------------------------------
    static constexpr int kSkulls = 3;
    static constexpr float kSkullPixels = 16.0f;
    static constexpr float kSkullOut = 50.0f, kSkullBreath = 20.0f;
    static constexpr float kSkullTurn = 10.0f * 0.17f;  // radians, LifeTime pinned at 10

    struct Gathering {
        bool alive = false;
        uint32_t owner = 0;
        float wait = 0.0f;     // seconds to its birth: the second is a frame behind the first
        int life = kGatherLife;
        float owed = 0.0f;     // reference frames not yet stepped
        float hand[3] = {};
        float shiny = 2.0f, shinyRoll = 0.0f;
    };
    struct Ribbon {
        bool alive = false;
        uint32_t owner = 0;
        float wait = 0.0f;     // seconds to the key it is made on
        bool made = false;
        int life = kRibbonLife;
        float owed = 0.0f;
        float light = 0.0f;
        float pivot[3] = {};
        float yaw = 0.0f;      // his, at the making
        float turn = kStartTurn;  // Direction[2] off his facing, degrees
        int tails = 0;
        float edge[kTails][2][3] = {};  // newest first
        float last[3] = {};
    };
    struct Bolt {
        bool alive = false;
        uint32_t owner = 0;    // a gathering's homes on his hand as he moves; 0 holds `to`
        float to[3] = {};
        float half = 0.05f;    // metres
        float left = 0.0f;     // frames
        float owed = 0.0f;
        float jitter = 1.0f;   // radians a move either way: 512 / Scale degrees
        float way[3] = {};
        int tails = 0;
        float trail[kBoltTails][3] = {};  // newest first
    };
    struct Spark {
        bool alive = false;
        float at[3] = {}, step[3] = {};  // metres, and metres a frame
        float scale = 0.1f, left = 0.0f;
    };
    struct Fire {
        bool alive = false;
        float at[3] = {}, drift[3] = {};
        float scale = 1.5f, left = 0.0f, gravity = 0.0f, spin = 0.0f;
    };
    struct Skull {
        float at[3] = {};
    };

    static constexpr int kCasts = 6;
    static constexpr int kBolts = 256;
    static constexpr int kSparks = 128;
    static constexpr int kFires = 64;
    static constexpr int kMostSkulls = 64 * kSkulls;

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle ribbonSheet_ = BGFX_INVALID_HANDLE, fireSheet_ = BGFX_INVALID_HANDLE,
                        boltSheet_ = BGFX_INVALID_HANDLE, sparkSheet_ = BGFX_INVALID_HANDLE,
                        shinySheet_ = BGFX_INVALID_HANDLE, skullSheet_ = BGFX_INVALID_HANDLE;
    Gathering gatherings_[kCasts * kGatherings] = {};
    Ribbon ribbons_[kCasts] = {};
    Bolt bolts_[kBolts] = {};
    Spark sparks_[kSparks] = {};
    Fire fires_[kFires] = {};
    Skull skulls_[kMostSkulls] = {};
    int skullCount_ = 0;
    float clock_ = 0.0f;   // seconds, for the skulls' breath and the bolts' scroll
    uint32_t dice_ = 0xf1a5e5u;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    // MU's local (x, y) on an angle `turn` degrees off our `yaw`, into our (x, z): fury.h's
    // mapping (MU's -y ahead, its -x to his left), turned first by MU's own rotation.
    static void local(float yaw, float turn, float x, float y, float out[2]);
    void bolt(uint32_t owner, const float from[3], const float to[3], float scale, float frames);
    void stepGathering(Gathering& one);
    template <typename Sound>
    void makeRibbon(Ribbon& one, Sound& sound);
    void stepRibbon(Ribbon& one);
    void stepBolt(Bolt& one);
};

template <typename Place, typename Hand, typename Sound, typename Cuts>
void FireSlash::update(float seconds, Place place, Hand hand, Sound sound, Cuts cuts) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    clock_ += seconds;
    if (clock_ > 1000.0f) clock_ -= 1000.0f;
    const float metre = perUnit();
    for (Gathering& one : gatherings_) {
        if (!one.alive) continue;
        float at[3];
        if (hand(one.owner, at)) {
            for (int k = 0; k < 3; ++k) one.hand[k] = at[k];
            one.hand[1] += kHandLift * metre;
        } else {
            float feet[3], yaw = 0.0f;
            if (!place(one.owner, feet, &yaw)) {
                one.alive = false;
                continue;
            }
            for (int k = 0; k < 3; ++k) one.hand[k] = feet[k];
            one.hand[1] += 1.2f;
        }
        if (one.wait > 0.0f) {
            one.wait -= seconds;
            if (one.wait > 0.0f) continue;
            one.owed = -one.wait * kReferenceFps;
            stepGathering(one);  // MU moves an effect on the frame it is made
        } else {
            one.owed += frames;
        }
        while (one.alive && one.owed >= 1.0f) {
            one.owed -= 1.0f;
            stepGathering(one);
        }
    }
    for (Ribbon& one : ribbons_) {
        if (!one.alive) continue;
        if (!one.made) {
            one.wait -= seconds;
            if (one.wait > 0.0f) continue;
            float feet[3], yaw = 0.0f;
            if (!place(one.owner, feet, &yaw)) {
                one.alive = false;
                continue;
            }
            for (int k = 0; k < 3; ++k) one.pivot[k] = feet[k];
            one.pivot[1] += kPivotLift * metre;
            one.yaw = yaw;
            one.owed = -one.wait * kReferenceFps;
            makeRibbon(one, sound);
            continue;
        }
        one.owed += frames;
        while (one.alive && one.owed >= 1.0f) {
            one.owed -= 1.0f;
            stepRibbon(one);
        }
    }
    for (Bolt& one : bolts_) {
        if (!one.alive) continue;
        if (one.owner != 0) {
            float at[3];
            if (hand(one.owner, at)) {
                for (int k = 0; k < 3; ++k) one.to[k] = at[k];
                one.to[1] += kHandLift * metre;
            }
        }
        one.owed += frames;
        while (one.alive && one.owed >= 1.0f) {
            one.owed -= 1.0f;
            stepBolt(one);
        }
    }
    for (Spark& one : sparks_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        for (int k = 0; k < 3; ++k) one.at[k] += one.step[k] * frames;
        one.scale += kSparkGrows * frames;
    }
    for (Fire& one : fires_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        one.gravity += kFireRises * frames;
        for (int k = 0; k < 3; ++k) one.at[k] += one.drift[k] * frames;
        one.at[1] += one.gravity * 10.0f * metre * frames;
        one.scale = std::max(0.0f, one.scale - kFireShrinks * frames);
    }
    // The skulls, laid fresh each frame over whatever stands cut now.
    skullCount_ = 0;
    const float breathAt = clock_ * 1000.0f * 0.0031f;
    cuts([&](const float crown[3]) {
        for (int i = 0; i < kSkulls && skullCount_ < kMostSkulls; ++i) {
            const float out = (kSkullOut + kSkullBreath * std::sin(float(i) * 15.37f + breathAt)) * metre;
            const float turn = float(i) * 120.0f * kDegrees + kSkullTurn;
            Skull& skull = skulls_[skullCount_++];
            skull.at[0] = crown[0] + std::cos(turn) * out;
            skull.at[1] = crown[1];
            skull.at[2] = crown[2] + std::sin(turn) * out;
        }
    });
}

template <typename Sound>
void FireSlash::makeRibbon(Ribbon& one, Sound& sound) {
    one.made = true;
    float feet[3] = {one.pivot[0], one.pivot[1] - kPivotLift * perUnit(), one.pivot[2]};
    sound(one.owner, feet);
    // Made and moved on the same frame, as BITMAP_SWORD_FORCE makes it in its first move.
    stepRibbon(one);
}

}  // namespace mu::game
