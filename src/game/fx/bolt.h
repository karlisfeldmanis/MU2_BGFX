// The wizard's Energy Ball: a bolt of light from the caster to what he threw it at.
//
// MU's whole spell is one line, and it is the `default` at the bottom of a thirty-monster switch
// in the damage arm of MoveCharacter (ZzzCharacter.cpp:5088), which is why it is so easily
// missed: `CreateEffect(BITMAP_ENERGY, o->Position, ..., 0, to)` -- created at the CASTER, chest
// high, aimed along the ground at the target, sixty units a reference frame, twenty frames of
// life. Every frame it lays another `BITMAP_ENERGY` behind it and a `BITMAP_SPARK + 1` at four
// times its sheet, and those live two frames: a wake, not a streak.
//
// **MU2's `client/core/Bolt.cs` is the second source and it is used deliberately**, as
// `fx/meteor.h` uses MU2's Meteor.cs: that file is this effect built in Godot and judged by eye,
// and its remarks record the traps -- `Scale` is a multiplier on the sheet (the halo is 1.28 m,
// not 0.32), the lay is one a reference frame by DISTANCE and not a coin, the wake turns, and an
// arrival pins the bolt rather than ending it. Two things in it are MU2's and not MU's, and are
// carried here marked: the core star drawn at the bolt itself, and the faint blue haze behind it.
//
// Nothing here decides a blow. The realm let the bolt go (`What::Loosed`) and lands it when its
// flight is over (`What::Hit`, thrown); this is what the player watches in between. Its
// `kTilesPerSecond` and `kStopsShort` are the realm's `kBoltTilesPerSecond` and `kBoltStopsShort`
// (sim/realm_fight.cpp) written twice, because `sim` may not include `game`: change one, change
// both, or the bolt and its damage arrive apart.
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
    // The three sheets off the cooked showing: `energy` (Thunder01, MU's BITMAP_ENERGY),
    // `spark_flash` (the sword strike's contact, which MU says in as many words is the same one),
    // and `flare_blue` for the haze. Any may be missing; what depends on it is not drawn.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);

    // A bolt let go, from the caster's feet toward the target's, in world metres; the chest
    // height is added here, where the client adds it. `target` is the body id, asked each frame
    // through `update`'s `alive` so a bolt at something that died in the air flies on past it.
    void cast(const float from[3], const float to[3], uint32_t target);

    // Advances everything by the frame's seconds. `alive(id)` is whether a target is still worth
    // arriving on; `where(id, out)` is where it stands this frame, in world metres, so the end of
    // the flight is measured against a body that may have walked -- the heading does not change,
    // which is MU's: the bolt is aimed once and does not home.
    template <typename Alive, typename Where>
    void update(float seconds, Alive alive, Where where);

    void gather(gfx::Effects& effects) const;

    // The blue it throws on the ground: `AddTerrainLight((lum*0.2, lum*0.4, lum*1.0), 2)`, lum
    // being the bolt's own `LifeTime * 0.2`. One a bolt in the air, up to `max`.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

    uint32_t flying() const;
    uint32_t refused() const { return refused_; }

    // The flight's two numbers, shared with the realm (`Realm::loose`): sixty units a reference
    // frame is fifteen tiles a second, and `CheckTargetRange` ends it a tile short.
    static constexpr float kTilesPerSecond = 15.0f;
    static constexpr float kStopsShort = 1.0f;

private:
    struct Head {
        bool alive = false;
        float at[3];
        float along[3];     // unit, flat
        float life;         // reference frames left
        float flown;        // metres since the last wake
        float hazed;        // and since the last puff of haze
        float coreRoll;     // the core star's own turn
        uint32_t target;
    };
    struct Mote {
        bool alive = false;
        float at[3];
        float velocity[3];  // metres a reference frame
        float scale;        // MU's `Scale`, which the shrink eats
        float width;        // the sheet's width in metres
        float shrink;       // of scale, a reference frame
        float roll, spin;   // radians, and radians a reference frame
        float life, full;   // reference frames
        float colour[3];
        bool dims;          // faded over its life (the haze), or cut off bright (MU's own)
        bgfx::TextureHandle sheet;
    };

    // ---- MU's numbers (ZzzEffect.cpp BITMAP_ENERGY, ZzzEffectParticle.cpp) -----------------
    static constexpr float kReference = 25.0f;  // MU's animation clock
    static constexpr float kPerMetre = 100.0f;   // MU units to the metre
    static constexpr float kSpeed = 60.0f;       // units a reference frame
    static constexpr float kLife = 20.0f;        // LifeTime, frames
    static constexpr float kChest = 100.0f;      // Position[2] += 100
    static constexpr float kArrives = 100.0f;    // CheckTargetRange
    static constexpr float kWakeFrames = 2.0f;   // CreateParticle's default LifeTime
    static constexpr float kWakeSpacing = kSpeed;  // one a reference frame, by distance
    static constexpr float kWakeTint = 0.2f;     // LifeTime * 0.2
    static constexpr float kWakeScale = 1.0f;
    static constexpr float kSmallest = 0.6f, kLargest = 1.3f;  // (rand() % 8 + 6) * 0.1
    static constexpr float kSparkScale = 4.0f;   // the halo, 128 units off a 32-pixel sheet
    static constexpr float kFlashScale = 6.0f;   // the contact
    static constexpr float kSpinDegrees = 20.0f; // o->Gravity, which is a spin rate
    static constexpr float kSparkShrink = 0.5f, kFlashShrink = 2.0f;
    static constexpr float kFlashSpeed = 50.0f;  // units a reference frame, along the heading
    static constexpr float kGone = 0.2f;         // the scale a spark stops at
    static constexpr float kGlow[3] = {0.2f, 0.4f, 1.0f};
    static constexpr float kGlowTiles = 2.0f;
    static constexpr float kGlowTint = 0.2f;

    // ---- MU2's, and marked (client/core/Bolt.cs) ------------------------------------------
    // The core: one star at the bolt itself for as long as it flies, a little over the wake's
    // size. MU draws nothing at the moving object; its bolt is the stamps it left behind.
    static constexpr float kCoreScale = kWakeScale * 1.15f;
    // The haze: a faint blue trail on flare_blue at a tenth of the head, so the two wake stamps
    // a metre apart read as one thing with a tail at any distance. Smoke was tried and is wrong.
    static constexpr float kHazeTint = 0.22f;
    static constexpr float kHazeScale = 1.6f;
    static constexpr float kHazeRate = 2.0f;     // a reference frame, by distance
    static constexpr float kHazeFrames = 14.0f;
    static constexpr float kHazeOpens = -0.06f;  // negative shrink: it grows
    static constexpr float kHazeRises = 4.0f;    // units a reference frame
    static constexpr float kHaze[3] = {0.35f, 0.45f, 1.0f};

    // Pools, sized once and never grown. MU2's 256 motes is a dozen bolts at once.
    static constexpr int kMaxBolts = 16;
    static constexpr int kMaxMotes = 256;

    bgfx::TextureHandle energy_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle haze_ = BGFX_INVALID_HANDLE;
    // The sheets' own widths in metres: 64, 32 and 64 pixels, which is what `Scale` multiplies.
    static constexpr float kEnergyWidth = 0.64f;
    static constexpr float kSparkWidth = 0.32f;
    static constexpr float kHazeWidth = 0.64f;

    Head heads_[kMaxBolts] = {};
    Mote motes_[kMaxMotes] = {};
    uint32_t refused_ = 0;

    // The drawing's own dice, never the sim's.
    uint32_t dice_ = 0x5EEDB017u;
    float unit();

    void lay(bgfx::TextureHandle sheet, float width, const float at[3], const float colour[3],
             float scale, float roll = 0.0f, float spin = 0.0f, float shrink = 0.0f,
             const float* velocity = nullptr, float life = kWakeFrames, bool dims = false);
    // One bolt's frame, and whether it is still in the air. `standing` is whether its target is
    // still alive and `there` where it is, when it is.
    bool fly(Head& head, float factor, bool standing, const float* there);
    void step(float factor);
};

template <typename Alive, typename Where>
void Bolt::update(float seconds, Alive alive, Where where) {
    const float factor = seconds * kReference;
    for (Head& head : heads_) {
        if (!head.alive) continue;
        float there[3];
        const bool standing = head.target != 0 && alive(head.target) && where(head.target, there);
        if (!fly(head, factor, standing, standing ? there : nullptr)) head.alive = false;
    }
    step(factor);
}

}  // namespace mu::game
