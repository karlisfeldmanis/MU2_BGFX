// The wizard's Energy Ball: a bolt of light from the caster to what he threw it at.
//
// MU's whole spell is one line, and it is the `default` at the bottom of a thirty-monster switch
// in the damage arm of MoveCharacter (ZzzCharacter.cpp:5088), which is why it is so easily
// missed: `CreateEffect(BITMAP_ENERGY, o->Position, ..., 0, to)` -- created at the CASTER, chest
// high, aimed along the ground at the target, sixty units a reference frame, twenty frames of
// life. Every frame it lays another `BITMAP_ENERGY` behind it and a `BITMAP_SPARK + 1` at four
// times its sheet, and those live two frames: a wake, not a streak.
//
// **MU2's `client/core/Bolt.cs` is the second source**, as `fx/meteor.h` uses MU2's Meteor.cs:
// its remarks record the traps -- `Scale` is a multiplier on the sheet (the halo is 1.28 m), the
// lay is one a reference frame by DISTANCE and not a coin, the wake turns. Its core star at the
// head is kept.
//
// **Ours, tuned on the bolt bench (`--bolt-every`) on 2026-09-28 at the user's asking -- "a nice
// trail, a perfect impact" -- and marked as ours wherever it stands:**
//   * The wake FADES over its two frames. Cut off bright, as MU's is, two stamps a metre apart
//     read as two balls flying side by side at this frame rate.
//   * **One ball.** The halo (`BITMAP_SPARK + 1` at four times its sheet) is drawn once, at the
//     head; MU lays it with every wake stamp, and a stamp a metre back carrying its own full
//     halo is the second ball. The wake is kept, smaller and dimmer, and it fades.
//   * **The tail is smoke** -- *"elegant trail smoke"*, the user's words, after sparkles were
//     tried and thrown out. MU's smoke01, a grey wisp on black that MU itself adds, laid close
//     behind the head and tinted the bolt's blue: each puff gathers, opens, turns, lifts a little
//     and thins away, and overlapping they build a mist that is densest where the ball has just
//     been. smoke02, the dragon's, is painted orange and made a brown band; MU2's haze on
//     flare_blue lay down as a solid beam; a ribbon on joint_energy was too thin to see.
//   * **The impact is on the body**, not a tile short of it: a white-violet starburst
//     (`lobby_spark`), a soft bloom (`light`), a PUFF of the same smoke thrown out all round and
//     slowed (*"and puff on impact"*), and a short flash on the ground. A shock ring on
//     magic_ground was tried and read as a daisy. The bolt ends there. MU's own ending -- the lifetime pinned
//     inside a tile, one contact spark along the heading -- flew the head on through.
//   * **It meets the body.** Aimed at the middle of what it was thrown at, not level at a man's
//     chest, and steered after it as it moves, so a Budge Dragon on the wing or a spider
//     side-stepping is struck where it is drawn. MU flies it level and aims it once, and on this
//     camera that read as a bolt passing under or beside the thing it hit.
//   * **A miss flies past.** The realm's `Missed` reaches the drawing a tile before the bolt
//     reaches the body, so the bolt told it missed goes on by, unsteered, and goes out.
//
// Nothing here decides a blow. The realm let the bolt go (`What::Loosed`) and lands it when its
// flight is over (`What::Hit`, thrown); this is what the player watches in between. Its
// `kTilesPerSecond` and `kStopsShort` are the realm's `kBoltTilesPerSecond` and `kBoltStopsShort`
// (sim/realm_fight.cpp) written twice, because `sim` may not include `game`: change one, change
// both, or the bolt and its damage arrive apart. The drawn impact is on the body, a tile past
// where the damage is decided, which at fifteen tiles a second is a fifteenth of a second.
//
// It is `game`: it knows a caster and a target. The pass it draws through knows neither.
#pragma once

#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Bolt {
public:
    // The sheets off the cooked showing. Any may be missing; what depends on it is not drawn.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);

    // A bolt let go, from the caster's feet -- the chest height is added here, where the client
    // adds it -- at `to`, the middle of the body it is thrown at, in world metres. `target` is
    // the body id, 0 for a fixed point (the bench's), arrived at as if something stood there.
    void cast(const float from[3], const float to[3], uint32_t target);
    // The realm said this one missed: the bolt in the air at `target` flies on past it.
    void miss(uint32_t target);

    // Advances everything by the frame's seconds. `alive(id)` is whether a target is still worth
    // arriving on; `where(id, out)` is the middle of it as drawn this frame, which the bolt
    // steers after and arrives on.
    template <typename Alive, typename Where>
    void update(float seconds, Alive alive, Where where);

    // `eye` is kept for what faces it; the sprites are billboards and do not need it.
    void gather(gfx::Effects& effects, const float eye[3]) const;

    // The blue each bolt throws on the ground under it (MU's `AddTerrainLight`), and the flash of
    // each impact (ours). Up to `max`.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

    uint32_t flying() const;
    uint32_t refused() const { return refused_; }

    // The flight's two numbers, shared with the realm (`Realm::loose`): sixty units a reference
    // frame is fifteen tiles a second, and `CheckTargetRange` decides it a tile short.
    static constexpr float kTilesPerSecond = 15.0f;
    static constexpr float kStopsShort = 1.0f;

private:
    struct Head {
        bool flying = false;  // in the air
        bool used = false;    // or still drawing its tail after it ended
        float at[3];
        float along[3];       // unit, flat
        float life;           // reference frames left
        float flown;          // metres since the last wake
        float smoked;         // and since the last puff of smoke
        float coreRoll;
        uint32_t target;      // 0 for the bench's fixed point, `aim`
        float aim[3];
        bool missing;         // the realm said it missed: fly on past
        bool passed;          // and it has: no more steering, going out
    };
    struct Mote {
        bool alive = false;
        float at[3];
        float velocity[3];  // metres a reference frame
        float drag;         // what a reference frame leaves of the velocity
        float scale;        // MU's `Scale`, which the shrink eats
        float width;        // the sheet's width in metres
        float shrink;
        float roll, spin;
        float life, full;   // reference frames
        float colour[3];
        bool dims;
        // Smoke is mixed and not added (`gfx::Blend::Dust`), and fades in before it fades out.
        bool smoke;
        float alpha;
        bgfx::TextureHandle sheet;
    };
    struct Burst {
        bool alive = false;
        float at[3];
        float age;   // reference frames
        float roll;
    };

    // ---- MU's numbers (ZzzEffect.cpp BITMAP_ENERGY, ZzzEffectParticle.cpp) -----------------
    static constexpr float kReference = 25.0f;
    static constexpr float kPerMetre = 100.0f;
    static constexpr float kSpeed = 60.0f;       // units a reference frame
    static constexpr float kLife = 20.0f;        // LifeTime, frames
    static constexpr float kChest = 100.0f;      // Position[2] += 100
    static constexpr float kWakeFrames = 2.0f;
    static constexpr float kWakeSpacing = kSpeed;
    static constexpr float kWakeTint = 0.2f;     // LifeTime * 0.2
    static constexpr float kSmallest = 0.6f, kLargest = 1.3f;
    static constexpr float kSparkScale = 4.0f;   // the halo
    static constexpr float kSpinDegrees = 20.0f;
    static constexpr float kSparkShrink = 0.5f;
    static constexpr float kGone = 0.2f;
    static constexpr float kGlow[3] = {0.2f, 0.4f, 1.0f};
    static constexpr float kGlowTiles = 2.0f;
    static constexpr float kGlowTint = 0.2f;

    // ---- ours (see the head of the file) -------------------------------------------------
    static constexpr float kCoreScale = 1.15f;   // MU2's core star, of the energy sheet
    // The smoke: a puff every so many metres, born small behind the head and opening as it
    // thins, lifting a little, on MU's smoke01 tinted toward the bolt's blue.
    static constexpr float kSmokeSpacing = 0.12f;       // metres
    static constexpr float kSmokeBorn = 0.32f, kSmokeGrows = 0.075f;  // scale, and a frame
    // Halved on 2026-09-28, *"energy ball trail was to long"*: 11 + 6 frames drew a tail most
    // of the way back to his hand.
    static constexpr float kSmokeFrames = 6.0f, kSmokeMore = 3.0f;  // life, and its spread
    static constexpr float kSmokeAlpha = 1.0f;
    static constexpr float kSmokeRises = 0.006f;        // metres a reference frame
    static constexpr float kSmokeTint[3] = {0.50f, 0.66f, 1.0f};
    static constexpr float kSmokeWidth = 0.64f;         // smoke02 is 64 pixels
    // The puff on impact: the same smoke, thrown out all round and slowed hard.
    static constexpr int kPuffs = 12;
    static constexpr float kPuffSlow = 1.2f, kPuffFast = 2.6f;  // metres a second
    static constexpr float kPuffAlpha = 1.0f;
    // The halo at the head, and how bright the wake stamps are beside the core.
    static constexpr float kHaloTint = 0.8f;
    static constexpr float kWakeDim = 0.55f;
    static constexpr float kWakeSmall = 0.45f, kWakeLarge = 0.85f;
    // How much of MU's ground light under a flying bolt: at a full four it bleached the cobbles.
    static constexpr float kGlowShare = 0.35f;
    // The impact, in reference frames and metres.
    static constexpr float kBurstFrames = 9.0f;     // how long the whole of it lasts
    static constexpr float kFlashFrames = 5.0f;
    static constexpr float kFlashFrom = 0.9f, kFlashTo = 1.8f;
    static constexpr float kBloomFrames = 4.0f, kBloomWide = 2.6f;
    static constexpr float kImpactGlow[3] = {0.28f, 0.38f, 0.85f};
    static constexpr float kImpactGlowTiles = 2.0f;
    // How near the body the head must come to have hit it: under a third of a metre, or past it.
    static constexpr float kStrikes = 0.3f;
    // How far a trail puff wanders off the line, in metres a reference frame: enough that the
    // wisp breaks up behind the ball instead of standing as a tube.
    static constexpr float kSmokeWander = 0.014f;
    // How hard it turns toward a body that moved, as a share of the gap a reference frame. Soft:
    // a bolt that snapped round would read as a guided missile, and it only has to take up a
    // side-step or a wingbeat.
    static constexpr float kSteer = 0.35f;
    // What is left of a missing bolt's life once it is past: a few frames to go out in.
    static constexpr float kPastFrames = 4.0f;

    static constexpr int kMaxBolts = 16;
    static constexpr int kMaxMotes = 384;
    static constexpr int kMaxBursts = 12;

    bgfx::TextureHandle energy_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle star_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bloom_ = BGFX_INVALID_HANDLE;
    // The sheets' own widths in metres, which is what `Scale` multiplies.
    static constexpr float kEnergyWidth = 0.64f;
    static constexpr float kSparkWidth = 0.32f;

    Head heads_[kMaxBolts] = {};
    Mote motes_[kMaxMotes] = {};
    Burst bursts_[kMaxBursts] = {};
    uint32_t refused_ = 0;

    // The drawing's own dice, never the sim's.
    uint32_t dice_ = 0x5EEDB017u;
    float unit();

    void lay(bgfx::TextureHandle sheet, float width, const float at[3], const float colour[3],
             float scale, float roll = 0.0f, float spin = 0.0f, float shrink = 0.0f,
             const float* velocity = nullptr, float life = kWakeFrames, bool dims = false,
             float drag = 1.0f, bool smoke = false, float alpha = 1.0f);
    void puff(const float at[3], const float velocity[3], float scale, float life, float alpha);
    // One bolt's frame, and whether it is still in the air.
    bool fly(Head& head, float factor, bool standing, const float* there);
    void strike(const float at[3], const float along[3]);
    void step(float factor);
};

template <typename Alive, typename Where>
void Bolt::update(float seconds, Alive alive, Where where) {
    const float factor = seconds * kReference;
    for (Head& head : heads_) {
        if (!head.flying) continue;
        {
            float there[3];
            bool standing = false;
            if (head.target == 0) {
                standing = true;
                for (int k = 0; k < 3; ++k) there[k] = head.aim[k];
            } else {
                standing = alive(head.target) && where(head.target, there);
            }
            if (!fly(head, factor, standing, standing ? there : nullptr)) {
                head.flying = false;
                head.used = false;
            }
        }
    }
    step(factor);
}

}  // namespace mu::game
