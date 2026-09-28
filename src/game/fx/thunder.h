// The wizard's Lightning: a jagged bolt drawn from where he casts to the body, re-rolled every
// frame for a third of a second, with a spark and a blue light where it bites.
//
// MU draws it as `BITMAP_JOINT_THUNDER` joints -- a wide one and a thin one, each a strip that
// walks toward the target in fifty-unit strides with a random turn at every step, re-thrown every
// frame for fifteen frames, its sheet scrolling along it (MU2's `client/core/Thunder.cs` is that
// walk ported, fourteen hundred lines). This is a smaller build of the same picture, marked as
// ours: the path between the two ends is re-rolled whole each frame -- points pinned at both ends
// and thrown off the line in between -- and drawn as two crossed quads a segment, so it has width
// from any angle without the camera. What is MU's: the sheet (JointThunder01), the two widths
// (fifty and ten units), the scroll, the energy spark on the body, the blue ground light,
// `SOUND_THUNDER01` on the cast. Not MU's smoke at the contact (smoke01 one frame in eight, which
// the user turned down); instead, ours, a little smoke rising off the bolt's own path as it goes
// out, as if the air it burned through were smoking (*"a little bit smoke after lightning is cast,
// which comes from the lightning itself"*, 2026-09-28).
//
// It does not fly: MU lands the blow on the cast (`Thunder.Flight` is nought), and so does the
// realm (`SkillRow::flies`). The push the blow gives is the realm's too (`Realm::push`).
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Thunder {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);

    // A bolt from `from` to the middle of body `target` (`to` is where it is now; 0 holds `to`).
    void strike(const float from[3], const float to[3], uint32_t target);

    // **The crackle on the caster himself** while he channels (the user, 2026-09-28: "add some
    // electric effect to the character itself"), ours: every couple of reference frames two small
    // sparks jump between random points round his body -- `feet` and his drawn `tall` -- and a blue
    // light flickers on him. Called every frame the channel runs; it stops when the calls stop.
    void crackle(const float feet[3], float tall, float seconds);

    // `alive(id)` and `where(id, out)` as the bolt takes them: the far end follows the body.
    template <typename Alive, typename Where>
    void update(float seconds, Alive alive, Where where);

    void gather(gfx::Effects& effects) const;
    void gatherSmoke(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    uint32_t striking() const;

private:
    static constexpr int kPoints = 16;   // room for the path, ends included
    static constexpr int kForkPoints = 5;
    struct Arc {
        bool alive = false;
        float from[3], to[3];
        uint32_t target;
        float left;                     // reference frames
        float reroll;                   // frames until the path is thrown again
        float wide[kPoints][3];         // the wide joint's path
        float thin[kPoints][3];         // and the thin one's, thrown on its own
        int points;                     // how many of each this throw uses
        float fork[kForkPoints][3];     // a branch off the side, when there is one
        bool forked;
        float spark;                    // the contact's roll and size, per frame
        float sparkRoll;
        float glow;
        bool small;                     // a crackle on the caster: thin, brief, no spark or light
        bool smoked;                    // the smoke is laid once, as it starts to go out
    };
    struct Puff {
        bool alive = false;
        float at[3];
        float drift[3];                 // metres a second
        float size;                     // metres across
        float spin;
        float left, full;               // reference frames
    };

    static constexpr float kReference = 25.0f;
    static constexpr float kFrames = 9.0f;        // a third of a second, lit
    static constexpr float kFadeFrames = 4.0f;    // and dimming over its last four
    static constexpr float kRerollFrames = 1.0f;  // a new path every reference frame, as MU
    static constexpr float kWide = 0.5f;          // metres: MU's fifty units
    static constexpr float kThin = 0.12f;         // and the thin joint's ten, a little wider
    // How far the path may wander off the line, in metres, before it is pinned back to both ends:
    // each throw rolls its own wildness in a band, walks away from the line step by step, and
    // takes off the drift so it ends where it must. More random than MU's fixed-stride walk on
    // the user's word ("more random so they all don't look the same").
    static constexpr float kJag = 0.30f;          // a step's wander, metres, at the middle
    static constexpr float kWildest = 1.6f, kTamest = 0.6f;
    static constexpr int kFewest = 8;             // points a throw, fewest and most
    static constexpr float kForkChance = 0.45f;   // a throw that splits a branch off
    static constexpr float kForkLongest = 1.3f, kForkShortest = 0.5f;  // metres
    static constexpr float kRepeats = 2.0f;       // the sheet twice along a bolt
    static constexpr float kScroll = 1.0f;        // sheet widths a second
    static constexpr float kWhite[3] = {0.85f, 0.9f, 1.0f};
    static constexpr float kSparkWidth = 0.64f;   // Thunder01
    static constexpr float kSparkSmallest = 0.6f, kSparkLargest = 1.3f;
    // The light where it bites: MU's (0.2, 0.2, 1) blue, brighter than its 0.16-0.28 roll so it is
    // seen ("most of DW spells are light emitters"), and a tile wider.
    static constexpr float kGlow[3] = {0.35f, 0.45f, 1.0f};
    static constexpr float kGlowTiles = 3.0f;
    static constexpr int kMost = 32;
    // The crackle: a pair every two reference frames, each three frames long, a third the width.
    static constexpr float kCrackleEvery = 2.0f, kCrackleFrames = 3.0f, kCrackleWidth = 0.35f;
    static constexpr int kCracklePair = 2;
    static constexpr float kCrackleRadius = 0.45f;      // metres round his middle
    static constexpr float kCrackleGlow[3] = {0.30f, 0.40f, 1.0f};
    static constexpr float kCrackleGlowTiles = 2.5f;
    // The smoke off the path: one puff at every point along the wide joint, born small and
    // faint, opening and lifting, gone in a little over a second. smoke01, added and tinted a
    // cool grey, as MU adds that sheet.
    static constexpr int kSmokeEvery = 1;
    static constexpr float kSmokeBorn = 0.5f, kSmokeGrows = 0.028f;   // metres, and a frame
    static constexpr float kSmokeFrames = 28.0f;
    static constexpr float kSmokeRise = 0.35f;                        // metres a second
    static constexpr float kSmokeDrift = 0.15f;                       // metres a second
    static constexpr float kSmokeTint[3] = {0.48f, 0.50f, 0.58f};
    static constexpr int kMostPuffs = 96;

    bgfx::TextureHandle joint_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    Puff puffs_[kMostPuffs] = {};
    Arc arcs_[kMost] = {};
    float crackleDue_ = 0.0f;       // reference frames to the next pair
    float crackleLit_ = 0.0f;       // seconds the light on him has left; 0 is off
    float crackleAt_[3] = {};
    float crackleRoll_ = 1.0f;
    float clock_ = 0.0f;

    uint32_t dice_ = 0x7A11B017u;
    float unit();
    void throwPath(Arc& arc);
    void step(Arc& arc, float frames);
    void smoke(const Arc& arc);
};

template <typename Alive, typename Where>
void Thunder::update(float seconds, Alive alive, Where where) {
    const float frames = seconds * kReference;
    clock_ += seconds * kScroll;
    if (clock_ >= 1.0f) clock_ -= float(int(clock_));
    for (Arc& arc : arcs_) {
        if (!arc.alive) continue;
        if (arc.target != 0 && alive(arc.target)) {
            float there[3];
            if (where(arc.target, there)) {
                for (int k = 0; k < 3; ++k) arc.to[k] = there[k];
            }
        }
        step(arc, frames);
    }
    crackleLit_ = std::max(0.0f, crackleLit_ - seconds);
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        for (int k = 0; k < 3; ++k) one.at[k] += one.drift[k] * seconds;
        one.size += kSmokeGrows * frames;
    }
}

}  // namespace mu::game
