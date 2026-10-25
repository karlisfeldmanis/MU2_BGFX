// Sleep: the Summoner's first curse, MU's AT_SKILL_ALICE_SLEEP (219), as its let-go draws it -- a
// violet circle opening under the caster and two violet rings turning round what it put to sleep
// (ZzzCharacter.cpp:5160-5207) -- and the violet a sleeper breathes off while it sleeps
// (eDeBuff_Sleep, ZzzCharacter.cpp:10888-10899). Here the Ancient rune Sleep's (sim/items.h
// kSleep...), said as Loosed with skill 219 (sim::skill::kSleep) on each body it put to sleep.
//
// **What is MU's, and kept** (docs/plans/ancient-runes/sleep.md 2.2-2.4; 25 frames a second, MU
// units of a centimetre):
//   - Under him, BITMAP_MAGIC + 1 subtype 11 (Effect/Magic_Ground2) in (0.7, 0.3, 0.8), laid on
//     the land, added: LifeTime 20, `(20 - LifeTime) x 0.15` tiles across -- nought to three in
//     its twenty frames -- full bright, fading over its last five (ZzzEffect.cpp:1216-1219,
//     :9787-9799, :9819-9822, :9868-9882).
//   - On each sleeper, MODEL_ALICE_BUFFSKILL_EFFECT and EFFECT2 (Effect/elshildring and
//     elshildring2 on macardkmono), in (0.8, 0.3, 0.9), a metre up its feet and following it:
//     LifeTime 34 and 35, Scale 0.1 and 0.15 growing 0.035 a frame, turning +8 and -8 degrees a
//     frame, Alpha and BlendMeshLight climbing 0.05 a frame while LifeTime > 20 and falling 0.05 a
//     frame after, added (MoveHandlers.cpp:2213-2336; ZzzObject.cpp:629-639). Each frame of each
//     ring, at its centre: two BITMAP_LIGHT (flare01) at scale 5 in (0.8, 0.1, 0.9) x Alpha; two
//     BITMAP_SHINY + 5 (shiny05) at 2 and 1, turning both ways, in (0.7, 0.6, 0.9) x Alpha; and
//     three BITMAP_JOINT_HEALING subtype 15 streaks (JointEnergy01) in (0.7, 0.5, 0.7), born 200
//     units out above the centre, LifeTime 10, two tails five wide, homing on the centre ten
//     degrees a frame and four units a frame faster each frame, the light x 1/1.08 a frame, each
//     frame a BITMAP_SHINY + 1 (Shiny02) sparkle at 0.8 to 1.4 where it is
//     (ZzzEffectJoint.cpp:567-573, :3598-3608).
//   - While it sleeps, one frame in two off a bone drawn at random, 20 units below it, a
//     BITMAP_TWINTAIL_WATER subtype 2 (Effect/water) in (0.8, 0.3, 0.9): scale 1.0 to 1.6,
//     scattered 20 units aside and 0 to 40 up, a random turn, rising 2 to 3 units a frame,
//     shrinking 0.026 a frame, its alpha 0.05 a frame down, its light x 1/1.02 a frame
//     (MoveHandlers.cpp:2338-2378; ZzzEffectParticle.cpp:1131-1141, :5253-5267). Drawn added, as
//     MU's EnableAlphaBlend is GL_ONE, GL_ONE_MINUS_SRC_COLOR on a black-backed sheet.
//   - SOUND_SUMMON_SKILL_SLEEP at the cast (Play's).
//
// **Ours.** The rings start at the roll, not 14 frames after it: MU's lead is the Summoner's cast
// pose, which he has not. The hand glow MU draws through her pose is not drawn (Weakness's
// precedent). A particle is this engine's sprite at MU's size (its sheet's pixels times its
// scale, in MU units). The circle is laid once a roll, under him, however many it put to sleep.
// Presentation only: the realm put them to sleep on the tick it said so; the held pose is
// play_show's. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Sleep {
public:
    // The two rings out of effects/sleep and the circle, flare, shiny, sparkle, streak and puff
    // sheets off the cooked showing. False when the rings or the circle are missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // The circle under him at `feet` (world metres, the ground there).
    void cast(const float feet[3]);
    // The two rings on body `id`, standing at `feet` now.
    void rings(uint32_t id, const float feet[3]);
    // A sleeper's puff at a point on one of its bones: one, called one reference frame in two.
    void puff(const float at[3]);

    // `feet(id, out)` says where a ringed body stands this frame, false once it is gone.
    template <typename Feet>
    void update(float seconds, const Feet& feet) {
        for (Ring& one : rings_) {
            if (!one.alive) continue;
            float at[3];
            if (feet(one.id, at)) {
                for (int k = 0; k < 3; ++k) one.feet[k] = at[k];
            }
        }
        step(seconds);
    }
    void gather(gfx::Effects& effects) const;

private:
    struct Circle {
        bool alive = false;
        float at[3] = {};
        float left = 0.0f;  // reference frames
    };
    struct Ring {
        bool alive = false;
        bool second = false;  // elshildring2's, else elshildring's
        uint32_t id = 0;
        float feet[3] = {};
        float left = 0.0f;
        float scale = 0.1f;
        float turn = 0.0f;   // degrees
        float alpha = 0.0f;  // Alpha and BlendMeshLight, which MU steps together
        float owed = 0.0f;   // streaks owed, in frames
    };
    struct Streak {
        bool alive = false;
        uint32_t ring = 0;  // index into rings_, whose centre it homes on
        float at[3] = {};
        float tail[3] = {};
        float heading[3] = {};
        float speed = 0.0f;  // MU units a frame
        float light = 1.0f;
        float left = 0.0f;
        float owed = 0.0f;
    };
    struct Mote {
        bool alive = false;
        bool puff = false;  // a sleeper's water, else a streak's Shiny02 sparkle
        float at[3] = {};
        float scale = 1.0f;
        float spin = 0.0f;  // radians
        float rise = 0.0f;  // MU units a frame
        float alpha = 1.0f;
        float light = 1.0f;
        float left = 0.0f;
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr int kCircles = 8;
    static constexpr int kRings = 2 * 24;
    static constexpr int kStreaks = 3 * 12 * kRings / 2;
    static constexpr int kMotes = 1024;

    void step(float seconds);
    void ringCentre(const Ring& one, float out[3]) const;
    void gatherCircle(gfx::Effects& effects, const Circle& one) const;
    void throwMote(bool puff, const float at[3], float light = 1.0f);
    float unit();

    const content::Ground* ground_ = nullptr;
    std::vector<EffectCorner> ring_[2];
    bgfx::TextureHandle band_ = BGFX_INVALID_HANDLE;    // macardkmono
    bgfx::TextureHandle circle_ = BGFX_INVALID_HANDLE;  // Magic_Ground2
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;   // flare01
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;   // shiny05
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;   // Shiny02
    bgfx::TextureHandle streak_ = BGFX_INVALID_HANDLE;  // JointEnergy01
    bgfx::TextureHandle water_ = BGFX_INVALID_HANDLE;   // water
    Circle circles_[kCircles] = {};
    Ring rings_[kRings] = {};
    Streak streaks_[kStreaks] = {};
    Mote motes_[kMotes] = {};
    uint32_t dice_ = 0x51EE9u;
};

}  // namespace mu::game
