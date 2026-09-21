// What a blow looks like: the landing cue, the blood it throws and the number it puts up.
//
// The sprint's spine, and the rule that holds it together is that **nothing here is ever a
// fact**. The roll, the damage and the death all resolve on the tick, in src/sim, exactly as
// sprint 5 built them. This decides only WHEN those are drawn and what they look like, and a
// cue that gets dropped costs a sound and a splash and never a number that mattered.
//
// It is `game` and not `gfx`: it knows about blows and targets. It is not `sim` either -- it
// holds no state the rules read, and the headless run, which draws nothing, never builds one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

// A blow that has landed on the tick and has not yet been shown.
//
// MU fires the swing, the sound and the number together on the swing's FIRST key, because
// `ReceiveAttackDamage` does all three in one handler while `AnimationFrame == 0`. That reads
// badly: on the first key the body is still blended most of the way into whatever it was
// doing, so the sound arrives before the arm does and the number appears while he is still
// standing. MU2 moved the hit sound and the number to halfway through the swing, and this
// copies that -- `docs/combat.md`, and it is chosen rather than transcribed, because MU has no
// impact frame for an ordinary melee attack at all.
struct Cue {
    uint32_t attacker = 0;
    uint32_t target = 0;
    int32_t damage = 0;
    bool miss = false;
    // Seconds left on the drawing's own clock. NOT the wall clock: MU2 found that at haste
    // every timed thing fell behind the simulation, because the animation was scaled and the
    // fuses were not. See Showing::advance.
    float fuse = 0.0f;
    // Which swing this cue belongs to. The attacker's swing is counted up as it starts one,
    // and a cue whose token no longer matches the swing the attacker is in drops itself --
    // which is what "gated on the clip still being the swing" means. A step cancels a swing
    // in this engine, so this case happens in ordinary play and is not theoretical.
    uint32_t token = 0;
};

class Showing {
public:
    // Reads the cooked .mus and takes the sheets it needs. Returns false only when the table
    // will not open; a sheet that is missing is reported and drawn as nothing, because a
    // fight with no blood in it is still a fight.
    bool open(const std::string& assetDir, content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // A blow resolved on the tick. Nothing is shown yet.
    void schedule(const Cue& cue);

    // Burns every fuse down by the drawing's own scaled delta and appends the ones that came
    // due to `due`, which the caller owns so that this allocates nothing. The caller then
    // checks each one against the attacker's clip and either drops it or lands it.
    void advance(float seconds, std::vector<Cue>& due);

    // The cue survived its gate: throw the blood and put up the number. `feet` is the
    // target's position in world metres and `height` is how tall it is, also in metres.
    //
    // **Every length is taken in units of the target**, which is the one departure from the
    // client this makes and MU2 made it first. MU's own blood is thrown into a band 90 to 154
    // units above the target's feet and its splashes are half a metre across -- chosen
    // against a 120-unit player, and put on an 80-unit spider it is a grey cloud bigger than
    // the animal, which reads as smoke rather than as a wound. MU knows each model's height
    // (`CreateCharacter` hands out the boxes by hand) and simply does not use it here.
    //
    // `onHero` is whether the BLOW landed on the hero, not who threw it -- MU2's Points.cs
    // colours a number by whose health it came off, so the hero reads his own pain in red and
    // everyone else's in orange, and his own misses in white against everyone else's grey.
    void land(const Cue& cue, const float feet[3], float height, float attackerYaw, bool onHero);

    // Ages everything alive, on the same scaled clock as the fuses.
    void update(float seconds);

    // Writes every live thing into the transparent pass. `right` is the camera's own
    // horizontal in world space, which the digits of a number are laid along.
    void gather(gfx::Effects& effects, const float right[3]) const;

    uint32_t liveParticles() const { return uint32_t(particles_.size()); }
    uint32_t liveNumbers() const { return uint32_t(numbers_.size()); }
    uint32_t pending() const { return uint32_t(cues_.size()); }
    uint32_t dropped() const { return dropped_; }
    void drop() { ++dropped_; }

    // How far into the swing the hit sound and the number go, as a fraction of the clip.
    // MU2's own half, and the nearest thing the client commits to is its melee SKILL effects
    // being gated on `AnimationFrame >= 3` of a seven-key attack.
    static constexpr float kLandingPoint = 0.5f;

private:
    // One blood splash. MU's own particle, in metres, with its lengths already scaled by the
    // target it came off.
    struct Particle {
        float position[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float size = 0.1f;
        // Reference frames left of MU's twelve. The sheet's quadrant is `(12 - life) / 3` in
        // integer arithmetic, so four quadrants are stepped at one every three frames and the
        // last is showing as the particle dies.
        float life = 12.0f;
    };

    // One damage number, or one `Miss`.
    struct Number {
        float position[3] = {0, 0, 0};  // where the bottom of the row sits, in metres
        int32_t value = 0;
        bool miss = false;
        // MU's own two, kept in ITS units and per ITS 25 Hz frame rather than converted, so
        // that they can be read against ZzzEffectPoint.cpp without arithmetic. `rise` falls by
        // 0.3 a frame and the number dies when it reaches zero; the alpha is `rise * 0.4`,
        // which holds at full for a second and then fades over the last third of one.
        float rise = 10.0f;
        float scale = 15.0f;
        // Points.cs's `Add`: red on the hero, orange on anyone else for a plain hit; white on
        // the hero, grey on anyone else for a miss. Kept as colour rather than a re-derived
        // flag because the pool doesn't otherwise remember who a number was for.
        float colour[3] = {1.0f, 1.0f, 1.0f};
    };

    bool open_ = false;
    content::Showing table_;
    bgfx::TextureHandle blood_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle digits_ = BGFX_INVALID_HANDLE;

    std::vector<Cue> cues_;
    std::vector<Particle> particles_;
    std::vector<Number> numbers_;
    uint32_t dropped_ = 0;

    // The same rule as the transparent pass: reserved once, and a full pool refuses rather
    // than growing. A fight that allocates is a fight that hitches.
    uint32_t seed_ = 0x9E3779B9u;
    // MU's own rand() stands in for nothing here -- the SIM's dice are seeded and reproducible
    // and these are not, deliberately: where a splash flies is not a fact and must never reach
    // the seeded log. A cheap xorshift, drawn only by the drawing.
    uint32_t next();
    float unit();  // 0..1
};

}  // namespace mu::game
