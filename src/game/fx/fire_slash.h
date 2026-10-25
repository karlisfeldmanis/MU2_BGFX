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
//     even LifeTime a blue BITMAP_JOINT_THUNDER sub 3 from the point, set off on the point's own
//     random turn, homing on where the hand was (scale 10, ten frames), on an odd one a
//     BITMAP_SPARK + 1 (Spark03) at
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
//     (life 24, scale 1.28-1.91 shrinking 0.04 a frame, drifting 3.2-4.7 units a frame the
//     joint's way, rising by a Gravity of 0.004 a frame, lit by the joint's Light as it is made
//     and never faded: its Luminosity is worked out and not written, ZzzEffectParticle.cpp:
//     4541-4549) and two blue JOINT_THUNDER sub 3 from the last point to this one (scale 5-14
//     and 4-11, five frames), the first from where the joint was made, 180 out. Its light is
//     1 as it is made, then LifeTime / 30 down to 10, then divided by 1.5 a frame; it lives
//     twenty-one frames (LifeTime 20 to 0); a tail's is ((18 - j) / 18) * 2 of that, the newest
//     brightest, 1.33 at the head on its first frame. The sheet is motion_blur_r2, U along the
//     tails (the head at 1), V across.
//   * **A JOINT_THUNDER sub 3** (ZzzEffectJoint.cpp:1114-1121, 4727-5005) walks its whole ten
//     tails EVERY frame: ten times it turns at most 50 degrees toward its target (MoveHumming),
//     is thrown off that by (rand() % 1024 - 512) / Scale degrees in its pitch and its turn, lays
//     a tail -- a cross of +-Scale/2 on that thrown frame's X and Z -- and steps Velocity (10)
//     along it; and once a frame it steps Velocity on its unthrown way first. So it is a tight
//     crackle hugging its target, walked afresh each frame, never a growing line; past 150 from
//     its target it dies. Lit (0.5, 0.5, 1) to the end, unfaded; U = (NumTails - j) / 9 * 2
//     less the clock's second, the second face's twice that again.
//   * **The skulls** (ReceiveMagic, WSclient.cpp:4364-4381; MoveHandlers.cpp:583-634): while a
//     body's eDeBuff_Defense stands, three BITMAP_SKULL sprites (Skull, 16 units) 200 of its
//     scale over its feet, 120 degrees apart at a fixed turn, each 50 + 20 sin(i * 15.37 +
//     WorldTime * 0.0031) out, breathing; SOUND_BLOODATTACK on the body as it is laid.
//
// **Ours, and marked.** A colour over one is drawn as a second layer of the same quad, so the
// ribbon's head is MU's 1.33 (MuMain's ImmediateRenderer takes the float unclamped; and this
// pass adds in linear light where MU added in the screen's, which the overbright head makes up
// for). The flames are Fire01 read through fs_effect's breath, the heat ramp softened, with
// each cell scaled to a whole flame, as fx/breath.cpp draws this same subtype (game/world/
// lamps.cpp kCellGain): added plain, its last
// three cells are a ninth and a nineteenth of the first and the flames went out after six
// frames.
// **No sparks and no glow** (the user, 2026-10-24: "get rid of those sparkles", after asking
// twice for them smaller): MU's Spark03 sparks and Shiny02 glow at the hand are not drawn.
// **And a little more epic** (the same words, "finetune other parts of that spell to look more
// little bit epic"), all ours: a warm light under the ribbon, fading with it (MU adds none); a
// flame at most steps of the sweep, not on a coin, a little larger and scattered off the line;
// the ribbon 90 either side
// of its line where MU's is 75; and its light falling by a quarter a frame under ten, not a
// third, so the crescent holds a few frames longer.
// **The gathering is on his blade, not round his hand** (the user, 2026-10-24: "can we attach
// that electricity effect and sparkle on weapon not just random offset which is not attached to
// anything?"): MU's points on a sphere of 120 round bone 33 hung in the air, so here each frame
// reads the blade's grip and tip, off the weapon's own mesh as fx/streak's ribbon does. Its
// bolts crackle between two points on the blade, walked afresh on it every MU frame, and stop
// at their target rather than curling round it.
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
#include "gfx/renderer.h"

namespace mu::game {

class FireSlash {
public:
    // The ribbon, the flames, the bolts and the skull, off the
    // showing's table. False when the ribbon's sheet could not be had.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A Fire Slash by `owner`: the gathering from now, the ribbon `release` seconds on, where he
    // stands and faces then -- his clip's key 3 as it is played.
    void cast(uint32_t owner, float release);

    // Every frame. `place(id, feet, &yaw)` where a body is drawn, false for none; `blade(id, grip,
    // tip)` its weapon's two ends in the world, false for none; `sound(id, feet)`
    // SOUND_SKILL_SWORD3 as a ribbon is made; `cuts(lay)` calls `lay(crown)` for each body whose
    // defence stands cut, `crown` the point 200 of its scale over its feet.
    template <typename Place, typename Blade, typename Sound, typename Cuts>
    void update(float seconds, Place place, Blade blade, Sound sound, Cuts cuts);

    void gatherEffects(gfx::Effects& effects) const;
    // The warm light under each ribbon (ours, the header), at most `max`.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;
    // ---- the gathering (BITMAP_GATHERING sub 1) ---------------------------------------------
    static constexpr int kGatherLife = 20;
    static constexpr int kGatherings = 2;           // AttackTime 1 and 2
    static constexpr int kGatherPoints = 3;
    // Ours, the blade (above): the share of it from the grip that the gathering keeps off (the
    // hilt), a bolt's throw (MU's 512 / Scale is 51 degrees at 10, which leaves the blade), and
    // its frames.
    static constexpr float kBladeFrom = 0.15f;
    static constexpr float kBladeThrow = 22.0f;
    static constexpr float kBladeBoltFrames = 3.0f;
    // ---- the ribbon (BITMAP_JOINT_FORCE sub 0) ----------------------------------------------
    static constexpr int kRibbonLife = 20;
    static constexpr int kTails = 18;               // MaxTails; NumTails stops at 17
    static constexpr int kStepsAFrame = 8;
    static constexpr float kPivotLift = 100.0f;
    static constexpr float kRadius = 145.0f;
    static constexpr float kHalfWidth = 90.0f;      // a tail's cross on its X: MU's +-75, ours wider
    static constexpr float kStartTurn = 75.0f;      // HeadAngle 45 + 30
    static constexpr float kStepTurn = 11.0f;
    // ---- its fire (BITMAP_FIRE sub 0) -------------------------------------------------------
    static constexpr float kFireLife = 24.0f, kFirePixels = 64.0f;
    static constexpr float kFireShrinks = 0.04f, kFireRises = 0.004f;
    static constexpr float kFireCellGain[4] = {1.0f, 8.8f, 19.0f, 19.0f};  // lamps.cpp's
    // Ours: fs_flame's heat at the joint's Light of 1, cooling by this share over a life -- MU's
    // later cells are its darker paint, which the gains above take out -- and the sheet read this
    // much fuller, so the whole tongue burns orange as MU's added paint does and not only a hot
    // core: MU's flames are broad, two thirds of the ribbon's width.
    static constexpr float kFireHeat = 0.6f, kFireCools = 0.2f, kFireFull = 4.0f;
    // Ours, a little more epic (the header): most steps a flame, a little larger and thrown up to
    // this many units off the step so they do not stand in a row of beads; the light's fall under
    // ten; and the warm light under the ribbon -- its reach in tiles, its colour, its strength at
    // the ribbon's full light.
    static constexpr float kFireChance = 0.8f, kFireBigger = 1.15f, kFireScatter = 30.0f;
    // Ours, smooth (the user, 2026-10-24: "without cut/crop effect"): a flame keeps to Fire01's
    // first two cells, the whole flames -- its last two are painted as red shreds -- easing from
    // the first into the second over its life rather than snapping, dies away over the last
    // share of its life, and thins softly into what is behind it, metres.
    static constexpr int kFireCells = 2;
    static constexpr float kFireDiesFrom = 0.5f, kFireSoft = 0.5f;
    static constexpr float kFadeUnderTen = 1.25f;   // MU's 1.5
    static constexpr float kLightTiles = 4.0f;
    static constexpr float kLightColour[3] = {1.0f, 0.45f, 0.12f};
    static constexpr float kLightGain = 1.4f;
    static constexpr float kMadeFrom = 180.0f;      // the joint is made 180 out (2283-2293)
    // ---- the bolts (BITMAP_JOINT_THUNDER sub 3) ---------------------------------------------
    static constexpr int kBoltTails = 10;           // MaxTails, laid every frame
    static constexpr float kBoltStep = 10.0f;       // Velocity, units a tail
    static constexpr float kBoltHoming = 50.0f;     // MoveHumming's turn, degrees a tail
    static constexpr float kBoltReach = 150.0f;     // dies past this from its target
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
        float grip[3] = {}, tip[3] = {};  // his blade this frame, metres
    };
    struct Ribbon {
        bool alive = false;
        uint32_t owner = 0;
        float wait = 0.0f;     // seconds to the key it is made on
        bool made = false;
        float age = 0.0f;      // MU frames since it was made, smooth (see lightAt)
        float sweep = 0.0f;    // steps of the sweep owed, eight a MU frame, laid as they fall due
        float light = 1.0f;    // MU's o->Light: (1, 1, 1) as it is made
        float pivot[3] = {};
        float yaw = 0.0f;      // his, at the making
        float turn = kStartTurn;  // Direction[2] off his facing, degrees
        int tails = 0;
        float edge[kTails][2][3] = {};  // newest first
        float last[3] = {};
    };
    struct Bolt {
        bool alive = false;
        float at[3] = {}, to[3] = {};   // metres; `to` fixed at its making, as MU's
        float yaw = 0.0f, pitch = 0.0f; // its unthrown way, radians (MU's Angle[2], Angle[0])
        float half = 0.05f;    // metres
        float left = 0.0f;     // moves left
        float owed = 0.0f;
        float jitter = 1.0f;   // radians a tail either way: 512 / Scale degrees
        int tails = 0;
        float trail[kBoltTails][4][3] = {};  // newest first, each tail's cross: X -/+, Z -/+
        int on = -1;           // the gathering whose blade it crackles on, or -1
        float from = 0.0f, upto = 1.0f;  // ...between these shares of it, grip to tip
    };
    struct Fire {
        bool alive = false;
        float at[3] = {}, drift[3] = {};
        float scale = 1.5f, left = 0.0f, gravity = 0.0f, light = 1.0f;
    };
    struct Skull {
        float at[3] = {};
    };

    static constexpr int kCasts = 6;
    static constexpr int kBolts = 256;
    static constexpr int kFires = 64;
    static constexpr int kMostSkulls = 64 * kSkulls;

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle ribbonSheet_ = BGFX_INVALID_HANDLE, fireSheet_ = BGFX_INVALID_HANDLE,
                        boltSheet_ = BGFX_INVALID_HANDLE, skullSheet_ = BGFX_INVALID_HANDLE;
    Gathering gatherings_[kCasts * kGatherings] = {};
    Ribbon ribbons_[kCasts] = {};
    Bolt bolts_[kBolts] = {};
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
    // A JOINT_THUNDER sub 3 from `from` to `to`, set off on `yaw` and `pitch` (radians).
    void bolt(const float from[3], const float to[3], float yaw, float pitch, float scale,
              float frames);
    void stepGathering(Gathering& one);
    // A bolt crackling on gathering `on`'s blade from share `from` to `upto` of it.
    void bladeBolt(int on, float from, float upto);
    // A point `share` of the way along a gathering's blade, grip to tip.
    static void onBlade(const Gathering& one, float share, float out[3]);
    template <typename Sound>
    void makeRibbon(Ribbon& one, Sound& sound);
    // Lays `steps` of the sweep (MU's eight a frame, here as they fall due).
    void laySteps(Ribbon& one, int steps);
    // The ribbon's light `age` MU frames after it is made: LifeTime / 30 down to ten, then a fifth
    // less a frame (MU's a third, ours), as a smooth curve rather than MU's 25 steps a second.
    static float lightAt(float age);
    // Walks a bolt's tails afresh and ages it `ages` MU frames.
    void stepBolt(Bolt& one, float ages = 1.0f);
};

template <typename Place, typename Blade, typename Sound, typename Cuts>
void FireSlash::update(float seconds, Place place, Blade blade, Sound sound, Cuts cuts) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    clock_ += seconds;
    if (clock_ > 1000.0f) clock_ -= 1000.0f;
    const float metre = perUnit();
    for (Gathering& one : gatherings_) {
        if (!one.alive) continue;
        if (!blade(one.owner, one.grip, one.tip)) {
            // Bare-handed or not drawn: a short upright line where his hand would be.
            float feet[3], yaw = 0.0f;
            if (!place(one.owner, feet, &yaw)) {
                one.alive = false;
                continue;
            }
            for (int k = 0; k < 3; ++k) one.grip[k] = one.tip[k] = feet[k];
            one.grip[1] += 1.0f;
            one.tip[1] += 1.6f;
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
            // Where the joint is made: 180 out on its own angle, the first bolts' start.
            float made[2];
            local(yaw, kStartTurn, 0.0f, -kMadeFrom, made);
            one.last[0] = one.pivot[0] + made[0] * metre;
            one.last[1] = one.pivot[1];
            one.last[2] = one.pivot[2] + made[1] * metre;
            one.age = -one.wait * kReferenceFps;
            makeRibbon(one, sound);
            continue;
        }
        // Smooth, not MU's 25 steps a second (the user, 2026-10-24: "finetune so it looks like
        // 60fps not 30fps"): the sweep laid as its steps fall due, the light off a smooth age.
        one.age += frames;
        one.sweep += frames * float(kStepsAFrame);
        const int due = int(one.sweep);
        one.sweep -= float(due);
        one.light = lightAt(one.age);
        laySteps(one, due);
        // Twenty-one frames: MU's LifeTime 20 to 0, dead below it.
        if (one.age >= float(kRibbonLife + 1)) one.alive = false;
    }
    for (Bolt& one : bolts_) {
        if (!one.alive) continue;
        if (one.on >= 0) {
            // On the blade: laid afresh on it every drawn frame, so it keeps to the blade as it
            // moves (the same day), aged by the frame's share of MU's.
            one.left -= frames;
            if (one.left <= 0.0f) {
                one.alive = false;
                continue;
            }
            stepBolt(one, 0.0f);
            continue;
        }
        one.owed += frames;
        while (one.alive && one.owed >= 1.0f) {
            one.owed -= 1.0f;
            stepBolt(one);
        }
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
    // Made and moved on the same frame, as BITMAP_SWORD_FORCE makes it in its first move: its
    // first step now, the rest as they fall due.
    one.light = 1.0f;
    laySteps(one, 1);
    one.light = lightAt(0.0f);
}

}  // namespace mu::game
